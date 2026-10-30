/* Kept tile memory breakdown (PC, sim objects): drives world_tiles_assemble
 * along the hwlog29 route (L4RA, free-roam scenery) and splits the decoded
 * bytes of the kept tiles into what the build used and what it decoded for
 * nothing: meshes hidden by the scenery selection, LOD stand-ins of near
 * tiles, prelight of far full models. Per-mesh struct overhead is shown apart.
 *
 *   built + run like run_tiles.sh (T=t_tilemem) */
#include "../../src/world_tiles.c"
#undef main

static size_t mesh_geo(const N2Mesh *m) {
    size_t nv = (size_t)m->nverts;
    return nv * (m->authored_normals ? 8 : 5) * 4 + (((size_t)m->nidx * 2 + 3) & ~(size_t)3) +
           (m->vcol ? nv * 4 : 0);
}

int main(int argc, char **argv) {
    const char *track = "STREAML4RA", *troot = "ux0:data/OpenUG2/TRACKS";
    int event = argc > 1 ? atoi(argv[1]) : -1;
    float radius = 400.0f;
    static const float way[][2] = { { -60, 4 }, { -80, 80 }, { -400, 160 }, { -640, -160 }, { -320, -640 },
                                    { 0, -640 }, { 480, -640 }, { 480, -480 }, { 160, -160 }, { -160, 320 } };
    const int nway = (int)(sizeof way / sizeof way[0]);
    N2Scene prev = { 0 }, prev_v = { 0 };
    double s_all = 0, s_hid = 0, s_stand = 0, s_vcol = 0, s_ovh = 0; int steps = 0;
    size_t pk_all = 0, pk_hid = 0;
    float px = way[0][0], py = way[0][1];
    for (int w = 1; w < nway; w++) {
        float dx = way[w][0] - px, dy = way[w][1] - py, len = sqrtf(dx * dx + dy * dy);
        int ns = (int)(len / 80.0f); if (ns < 1) ns = 1;
        for (int i = 0; i < ns; i++) {
            float fx = px + dx * (float)i / (float)ns, fy = py + dy * (float)i / (float)ns;
            N2Scene sc = { 0 }, vi = { 0 };
            if (world_tiles_assemble(&sc, &vi, troot, track, fx, fy, radius, event, NULL) <= 0) {
                printf("step %d: assemble failed\n", steps); return 1;
            }
            size_t all = 0, hid = 0, stand = 0, vcol = 0, ovh = 0;
            for (int t = 0; t < g_wt_nloaded; t++) {
                const WtLoaded *tl = &g_wt_loaded[t];
                float tcx = ((float)tl->tx + 0.5f) * WTILE_SIZE - fx, tcy = ((float)tl->ty + 0.5f) * WTILE_SIZE - fy;
                int near = tcx * tcx + tcy * tcy < WT_LOD_NEAR_M * WT_LOD_NEAR_M;
                for (int k = 0; k < tl->n; k++) {
                    const N2Mesh *m = &tl->m[k];
                    size_t b = mesh_geo(m);
                    all += b; ovh += sizeof(N2Mesh) + sizeof(N2SharedGeometry);
                    if (m->scen_has && sel.count && !wg_selection_visible(&sel, m->scen_region, m->scen_index)) hid += b;
                    else if (near && m->world_role == 2) stand += b;
                    else if (!near && m->world_role == 1 && m->vcol) vcol += (size_t)m->nverts * 4;
                }
            }
            printf("step %3d at (%5.0f, %5.0f): %2d tiles, geometry %6zu KB: hidden %5zu, near stand-ins %5zu, "
                   "far prelight %5zu | mesh structs %5zu KB\n", steps, fx, fy, g_wt_nloaded, all >> 10, hid >> 10,
                   stand >> 10, vcol >> 10, ovh >> 10);
            s_all += all; s_hid += hid; s_stand += stand; s_vcol += vcol; s_ovh += ovh; steps++;
            if (all > pk_all) { pk_all = all; pk_hid = hid; }
            wt_scene_drop(&prev); wt_scene_drop(&prev_v);
            prev = sc; prev_v = vi;
            world_tiles_aim(px + dx * (float)(i + 1) / (float)ns, py + dy * (float)(i + 1) / (float)ns);
            SDL_Delay(200);
        }
        px = way[w][0]; py = way[w][1];
    }
    printf("event %d, %d steps, avg geometry %.0f KB: hidden %.0f (%.0f%%), near stand-ins %.0f (%.0f%%), far prelight %.0f "
           "(%.0f%%), mesh structs %.0f KB; peak %zu KB with %zu hidden\n", event, steps, s_all / steps / 1024,
           s_hid / steps / 1024, 100 * s_hid / s_all, s_stand / steps / 1024, 100 * s_stand / s_all,
           s_vcol / steps / 1024, 100 * s_vcol / s_all, s_ovh / steps / 1024, pk_all >> 10, pk_hid >> 10);
    return 0;
}
