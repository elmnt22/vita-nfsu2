#include "world_resident.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#include "physics.h"

#if defined(__vita__) && !defined(OPENUG2_SIM_PRE_H)
#define HITCH_THREAD 1   /* the frame thread counters in hitch_report */
#include <psp2/kernel/threadmgr.h>
#endif
#ifdef __vita__
void vita_mark(const char *fmt, ...);
#define VMARK(...) vita_mark(__VA_ARGS__)
#else
#define VMARK(...) ((void)0)
#endif

int world_resident_policy_valid(const WResidentPolicy *policy) {
    if (!policy || !isfinite(policy->resident_radius) ||
        !isfinite(policy->draw_radius) ||
        !isfinite(policy->safety_margin) ||
        !isfinite(policy->cell_size)) return 0;
    return policy->draw_radius > 0.0f && policy->safety_margin >= 0.0f &&
           policy->cell_size > 0.0f &&
           policy->resident_radius >= policy->draw_radius +
                                      policy->cell_size +
                                      policy->safety_margin;
}

static float resident_distance2(const float a[2], const float b[2]) {
    float x = a[0]-b[0], y = a[1]-b[1];
    return x*x+y*y;
}

int world_resident_candidate_useful(const WResidentPolicy *policy,
                                   const float active[2], const float candidate[2],
                                   const float player[2], const float predicted[2]) {
    if (!world_resident_policy_valid(policy) || !active || !candidate ||
        !player || !predicted) return 0;
    for (int i=0;i<2;i++)
        if (!isfinite(active[i]) || !isfinite(candidate[i]) ||
            !isfinite(player[i]) || !isfinite(predicted[i])) return 0;
    float reach = policy->resident_radius-policy->safety_margin;
    float current = resident_distance2(player, candidate);
    if (current > reach*reach) return 0;
    /* Preserve a useful prepared area when a prediction crosses another cell.
       It must cover the car and improve current or predicted coverage. Actual
       ground support is still checked before upload and activation. */
    return current+400.0f < resident_distance2(player, active) ||
           resident_distance2(predicted, candidate)+400.0f <
               resident_distance2(predicted, active);
}

int world_resident_target_behind(const float offset[2], const float velocity_mps[2]) {
    if (!offset || !velocity_mps) return 0;
    float d2=offset[0]*offset[0]+offset[1]*offset[1];
    float v2=velocity_mps[0]*velocity_mps[0]+velocity_mps[1]*velocity_mps[1];
    float dot=offset[0]*velocity_mps[0]+offset[1]*velocity_mps[1];
    /* More than 120 degrees behind. A near-perpendicular target on a bend
       should not cancel several seconds of useful work. */
    return isfinite(d2) && isfinite(v2) && isfinite(dot) &&
           d2 > 100.0f*100.0f && v2 > 8.0f*8.0f && dot < -0.5f*sqrtf(d2*v2);
}

void world_resident_streaming_selftest(void) {
    WResidentPolicy p={350,220,50,80};
    const float active[2]={320,-640}, candidate[2]={160,-400};
    const float player[2]={278,-500}, predicted[2]={160,-320};
    assert(world_resident_candidate_useful(&p,active,candidate,player,predicted));
    const float away[2]={480,-640}, outside[2]={-400,-400};
    assert(!world_resident_candidate_useful(&p,active,away,player,predicted));
    assert(!world_resident_candidate_useful(&p,active,outside,player,predicted));
    const float south[2]={0,-25}, bend[2]={-166,16}, reverse[2]={0,150};
    assert(!world_resident_target_behind(bend,south));
    assert(world_resident_target_behind(reverse,south));
    const float slow[2]={0,-2}, nearby[2]={0,50};
    assert(!world_resident_target_behind(reverse,slow));
    assert(!world_resident_target_behind(nearby,south));
}

int world_resident_target(const WResidentPolicy *policy,
                          float player_x, float player_y,
                          float active_x, float active_y,
                          float out_center[2]) {
    if (!world_resident_policy_valid(policy) || !out_center ||
        !isfinite(player_x) || !isfinite(player_y) ||
        !isfinite(active_x) || !isfinite(active_y)) return 0;

    float dx = player_x - active_x;
    float dy = player_y - active_y;
    float trigger2 = policy->cell_size * policy->cell_size;
    if (dx * dx + dy * dy <= trigger2) return 0;

    float target_x = floorf(player_x / policy->cell_size + 0.5f) *
                     policy->cell_size;
    float target_y = floorf(player_y / policy->cell_size + 0.5f) *
                     policy->cell_size;
    if (target_x == active_x && target_y == active_y) return 0;

    out_center[0] = target_x;
    out_center[1] = target_y;
    return 1;
}

int world_resident_route_point(const WResidentPolicy *policy,
                               const WorldResident *active,
                               const WorldCity *city,
                               float from_x, float from_y, float reference_z,
                               const float (*visited)[2], int visited_count,
                               float out_pos[3], float out_center[2]) {
    if (!world_resident_policy_valid(policy) || !active || !city ||
        !city->nav || city->nnav <= 0 || !out_pos || !out_center ||
        !isfinite(from_x) || !isfinite(from_y) || !isfinite(reference_z) ||
        visited_count < 0 || (visited_count && !visited)) return 0;

    int best = -1, best_cat = WSURF_NONE;
    float best_d2 = 1e30f, best_z = 0.0f, best_center[2] = {0.0f, 0.0f};
    float max_d2 = policy->draw_radius * policy->draw_radius;
    for (int i = 0; i < city->nnav; i++) {
        float x = city->nav[i*2], y = city->nav[i*2+1];
        float dx = x - from_x, dy = y - from_y;
        float d2 = dx*dx + dy*dy;
        if (!isfinite(x) || !isfinite(y) || d2 > max_d2) continue;
        if (visited_count >= 2) {
            float route_dx = active->center[0] - visited[visited_count-2][0];
            float route_dy = active->center[1] - visited[visited_count-2][1];
            float dot = dx*route_dx + dy*route_dy;
            float route2 = route_dx*route_dx + route_dy*route_dy;
            if (dot <= 0.5f * sqrtf(d2 * route2)) continue;
        }
        float center[2];
        if (!world_resident_target(policy, x, y,
                                   active->center[0], active->center[1], center))
            continue;
        int seen = 0;
        for (int j = 0; j < visited_count; j++)
            if (center[0] == visited[j][0] && center[1] == visited[j][1]) {
                seen = 1;
                break;
            }
        if (seen) continue;
        WGroundHit hit;
        int cat = world_ground_hit(&active->world.scene, x, y, reference_z, &hit);
        if (cat == WSURF_NONE) continue;
        if (best < 0 || cat < best_cat || (cat == best_cat && d2 < best_d2)) {
            best = i;
            best_cat = cat;
            best_d2 = d2;
            best_z = hit.z;
            best_center[0] = center[0];
            best_center[1] = center[1];
        }
    }
    if (best < 0) return 0;
    out_pos[0] = city->nav[best*2];
    out_pos[1] = city->nav[best*2+1];
    out_pos[2] = best_z;
    out_center[0] = best_center[0];
    out_center[1] = best_center[1];
    return 1;
}

static void resident_free_normals(WorldResidentResources *resources);

void world_resident_resources_free(WorldResidentResources *resources) {
    if (!resources) return;
    upload_world_batches_cancel(&resources->upload);
    upload_world_batches_cancel(&resources->cpu_plan);
    free(resources->mesh_tex); resources->mesh_tex = NULL; resources->mesh_tex_n = 0;
    free(resources->trim_list); resources->trim_list = NULL; resources->trim_n = 0;
    resident_free_normals(resources);
    /* World textures belong to the process-wide key cache in world.c and are
       shared with every other resident: this array only borrows them. The
       grass upload is this build's own, so it is still released here. */
    if (resources->terrain_texture) {
        int shared = 0;
        for (int i = 0; i < resources->texture_count; i++)
            if (resources->textures &&
                resources->textures[i] == resources->terrain_texture) shared = 1;
        if (!shared) glDeleteTextures(1, &resources->terrain_texture);
    }
    render_batch_array_free(&resources->ordinary, &resources->ordinary_count);
    render_batch_array_free(&resources->sky, &resources->sky_count);
    render_batch_array_free(&resources->glow, &resources->glow_count);
    render_batch_array_free(&resources->vista, &resources->vista_count);
    free(resources->texture_keys);
    free(resources->textures);
    free(resources->texture_modes);
    free(resources->mesh_textures);
    free(resources->mesh_modes);
    free(resources->mesh_batch);
    free(resources->debug_batches);
    free(resources->vista_mesh);
    free(resources->obstacles);
    free(resources->obstacle_z);
    free(resources->obstacle_src);
    phys_wall_index_drop(resources->wall_index);
    memset(resources, 0, sizeof *resources);
}

static int resident_vista_batches(WorldResidentResources *resources,
                                  const WorldNeighborhood *neighborhood) {
    for (int i = 0; i < neighborhood->vista.count; i++) {
        N2Scene one = { &neighborhood->vista.meshes[i], 1, 1 };
        GLuint texture = 0;
        for (int j = 0; j < resources->texture_count; j++)
            if (resources->texture_keys[j] == one.meshes[0].texkey) {
                texture = resources->textures[j];
                break;
            }
        N2Batch *part = NULL;
        int count = upload_cat_batches(&one, one.meshes[0].cat,
                                       &texture, &part, NULL);
        if (count < 0) return 0;
        if (!count) { free(part); continue; }
        int total = resources->vista_count + count;
        N2Batch *batches = (N2Batch *)realloc(
            resources->vista, (size_t)total * sizeof *batches);
        int *sources = (int *)realloc(resources->vista_mesh,
                                      (size_t)total * sizeof *sources);
        if (!batches || !sources) {
            if (batches) resources->vista = batches;
            if (sources) resources->vista_mesh = sources;
            render_batch_array_free(&part, &count);
            return 0;
        }
        resources->vista = batches;
        resources->vista_mesh = sources;
        memcpy(resources->vista + resources->vista_count, part,
               (size_t)count * sizeof *part);
        for (int j = 0; j < count; j++)
            resources->vista_mesh[resources->vista_count+j] = i;
        resources->vista_count = total;
        free(part); /* handles moved into resources->vista */
    }
    return 1;
}

#ifdef __vita__
/* Finish step: hand the worker's per-mesh normals / packed arrays to the
   worker to free at its next build start (hwlog51_L: their free() calls were
   2.3 ms of a finish frame). Locked: the lists are swapped, not shared. */
typedef struct { float **normals; PackedMesh *packed; int n; } ResidentDeferred;
static ResidentDeferred *g_rdef; static int g_rdef_n, g_rdef_cap;
static volatile int g_rdef_lock;
static void rdef_lock(void) { while (__sync_lock_test_and_set(&g_rdef_lock, 1)) { } }
static void rdef_unlock(void) { __sync_lock_release(&g_rdef_lock); }
static int resident_defer_normals(WorldResidentResources *r) {
    if (!r->mesh_normals && !r->mesh_packed) return 1;
    int ok = 0;
    rdef_lock();
    if (g_rdef_n == g_rdef_cap) {
        int c = g_rdef_cap ? g_rdef_cap * 2 : 8;
        ResidentDeferred *q = (ResidentDeferred *)realloc(g_rdef, (size_t)c * sizeof *q);
        if (q) { g_rdef = q; g_rdef_cap = c; }
    }
    if (g_rdef_n < g_rdef_cap) {
        ResidentDeferred *d = &g_rdef[g_rdef_n++];
        d->normals = r->mesh_normals; d->packed = r->mesh_packed; d->n = r->normals_count;
        ok = 1;
    }
    rdef_unlock();
    if (!ok) return 0;
    r->mesh_normals = NULL; r->mesh_packed = NULL; r->normals_count = 0;
    return 1;
}
/* Worker, at a build start. */
static void resident_deferred_flush(void) {
    rdef_lock();
    ResidentDeferred *list = g_rdef; int n = g_rdef_n;
    g_rdef = NULL; g_rdef_n = g_rdef_cap = 0;
    rdef_unlock();
    for (int k = 0; k < n; k++) {
        ResidentDeferred *d = &list[k];
        if (d->normals) for (int i = 0; i < d->n; i++) free(d->normals[i]);
        free(d->normals);
        if (d->packed) for (int i = 0; i < d->n; i++)
            if (!d->packed[i].borrowed) free(d->packed[i].v);
        free(d->packed);
    }
    free(list);
}
#endif

static uint32_t wrb_ms(struct timespec *a, struct timespec *b) {
    return (uint32_t)((b->tv_sec - a->tv_sec) * 1000 +
                      (b->tv_nsec - a->tv_nsec) / 1000000);
}

static void resident_free_normals(WorldResidentResources *resources) {
    if (resources->mesh_normals)
        for (int i = 0; i < resources->normals_count; i++) free(resources->mesh_normals[i]);
    free(resources->mesh_normals);
    resources->mesh_normals = NULL;
    if (resources->mesh_packed)
        for (int i = 0; i < resources->normals_count; i++)
            if (!resources->mesh_packed[i].borrowed) free(resources->mesh_packed[i].v);
    free(resources->mesh_packed);
    resources->mesh_packed = NULL;
    resources->normals_count = 0;
}

#ifdef __vita__
static int resident_validate_meshes(const WorldResident *resident);
/* Worker-side CPU work that used to run on the render thread while a
   resident was being finished: mesh validation, collision walls and the
   per-vertex normals batch upload needs. Nothing here touches GL. */
int world_build_aborted(void);
static SDL_atomic_t resident_tex_fetch;   /* see world_resident_texture_fetch */
static int resident_worker_precompute(WorldResident *candidate) {
    /* The mesh arrays grew by doubling; give the unused tail back. newlib
       shrinks in place, and a failed shrink just keeps the larger array. */
    for (int pass = 0; pass < 2; pass++) {
        N2Scene *s = pass ? &candidate->world.vista : &candidate->world.scene;
        if (s->meshes && s->count > 0 && s->cap > s->count + 16) {
            int before = s->cap;
            N2Mesh *m = (N2Mesh *)realloc(s->meshes, (size_t)s->count * sizeof *m);
            if (m) {
                /* the ground grid points into this array */
                if (!pass && candidate->world.grid.meshes == s->meshes) candidate->world.grid.meshes = m;
                s->meshes = m; s->cap = s->count;
            }
            if (!pass) VMARK("  worker: mesh records %d -> %d slots (%ld KB freed)", before, s->cap,
                             (long)(before - s->cap) * (long)sizeof(N2Mesh) >> 10);
        }
    }
    if (!resident_validate_meshes(candidate)) return 0;
    candidate->meshes_validated = 1;
    VMARK("  worker: meshes validated");
    if (world_build_aborted()) return 0;
    WorldResidentResources *r = &candidate->resources;
    const N2Scene *scene = &candidate->world.scene;
    int n = scene->count;
    r->obstacles = (float (*)[4])calloc((size_t)n, sizeof *r->obstacles);
    r->obstacle_z = (float (*)[2])calloc((size_t)n, sizeof *r->obstacle_z);
    r->obstacle_src = (int *)calloc((size_t)n, sizeof *r->obstacle_src);
    if (!r->obstacles || !r->obstacle_z || !r->obstacle_src) return 0;
    r->obstacle_count = phys_collect_walls(scene, r->obstacles, r->obstacle_src,
                                           r->obstacle_z, n);
    /* their broad-phase index too: activation only installs it (hwlog28:
       building it there was 5-6 ms of the frame) */
    r->wall_index = phys_wall_index_make((const float (*)[4])r->obstacles,
                                         (const float (*)[2])r->obstacle_z, r->obstacle_count);
    VMARK("  worker: %d collision walls", r->obstacle_count);
    if (world_build_aborted()) return 0;
    SDL_AtomicSet(&resident_tex_fetch, 1);
    world_predecode_textures(&candidate->world);
    SDL_AtomicSet(&resident_tex_fetch, 0);
    VMARK("  worker: textures predecoded");
    if (world_build_aborted()) return 0;
    /* whole meshes in batch vertex layout: the render thread then only
       concatenates them (a mesh that fails to pack is packed there instead) */
    r->mesh_packed = (PackedMesh *)calloc((size_t)n, sizeof *r->mesh_packed);
    if (r->mesh_packed) {
        int reused_packed = 0;
        r->normals_count = n; /* cleanup also owns a partially packed abort */
        free(r->trim_list); r->trim_n = 0;
        r->trim_list = (int *)malloc((size_t)(n ? n : 1) * sizeof *r->trim_list);
        for (int i = 0; i < n; i++) {
            if ((i & 31) == 0 && world_build_aborted()) return 0;
            const N2Mesh *m = &scene->meshes[i];
            if (m->world_role == 1 || m->world_role == 3 || m->cat == N2_SKY || m->cat == N2_GLOW) {
                /* not world batches (3: smokeable.c draws it) -- but a prop can
                   share a geometry another build packed: the trim frees it */
                if (r->trim_list && m->shared_geometry && m->shared_geometry->packed)
                    r->trim_list[r->trim_n++] = i;
                continue;
            }
            if (m->shared_geometry && m->shared_geometry->packed) reused_packed++;
            render_pack_mesh(m, &r->mesh_packed[i]);
            if (r->trim_list && m->shared_geometry && m->shared_geometry->packed)
                r->trim_list[r->trim_n++] = i;
        }
        VMARK("  worker: %d packed meshes reused", reused_packed);
    }
    VMARK("  worker: meshes packed");
    if (world_build_aborted()) return 0;
    Uint64 plan_begin=SDL_GetPerformanceCounter();
    GLuint *logical=calloc((size_t)n,sizeof *logical);
    unsigned char *modes=calloc((size_t)n,sizeof *modes);
    free(r->mesh_tex); r->mesh_tex = NULL; r->mesh_tex_n = 0;
    if (logical && modes && r->mesh_packed)
        r->mesh_tex=(ResidentMeshTex *)malloc((size_t)(n ? n : 1)*sizeof *r->mesh_tex);
    if (logical && modes && r->mesh_packed) {
        for (int i=0; i<n; i++) {
            const N2Mesh *m=&scene->meshes[i]; unsigned char mode=0;
            int resolved=world_predecoded_texture_info(m->texkey,&mode);
            logical[i]=resolved ? m->texkey : m->cat==N2_TERRAIN ? UINT32_MAX : 0;
            modes[i]=resolved ? n2_world_draw_mode(m,mode) : N2_DRAW_OPAQUE;
            if (r->mesh_tex) {
                ResidentMeshTex *t=&r->mesh_tex[i];
                t->key=m->texkey; t->resolved=(unsigned char)(resolved!=0);
                t->raw=mode; t->mode=modes[i]; t->pad=(unsigned char)((m->cat==N2_TERRAIN) |
                    /* 2: not in the plan (upload_world_batches_begin's filter) */
                    ((m->world_role==1 || m->world_role==3 || m->cat==N2_SKY || m->cat==N2_GLOW) << 1));
            }
        }
        if (r->mesh_tex) r->mesh_tex_n=n;
        r->cpu_plan=upload_world_batches_prepare_cpu(scene,
            (const float (*)[4])candidate->world.mbb,logical,modes,r->mesh_packed);
    }
    free(logical); free(modes);
    VMARK("  worker: batch plan %s in %u ms",r->cpu_plan ? "prepared" : "fallback",
        (unsigned)((SDL_GetPerformanceCounter()-plan_begin)*1000/SDL_GetPerformanceFrequency()));
    if (r->cpu_plan) {
        Uint64 f0=SDL_GetPerformanceCounter();
        int filled=render_world_plan_fill(r->cpu_plan);
        VMARK("  worker: %d meshes packed for batches the GPU lacks, %u ms",filled,
            (unsigned)((SDL_GetPerformanceCounter()-f0)*1000/SDL_GetPerformanceFrequency()));
    }
    extern void vita_mark_stats(void);
    vita_mark_stats();
    if (world_build_aborted()) return 0;
    return 1;
}
#endif

#ifdef __vita__
/* The same off-thread work for a prepare run outside a job (the track
   switch's thread): its finish then only uploads. */
int world_resident_precompute(WorldResident *candidate) {
    return resident_worker_precompute(candidate);
}
#endif

#define HITCH_MAX 160
static struct { const char *label; Uint64 t; } g_hitch[HITCH_MAX];
static int g_hitch_n = -1;   /* -1: no open span */
static Uint64 g_hitch_t0;
#ifdef HITCH_THREAD
/* The frame thread's own counters over the span: how often another thread
   or an interrupt took its core, how often it waited (a lock, a sleep), and
   its time inside malloc / free (sdl_vita.c) -- a stage that runs long with
   no work of its own to show for it was waiting on one of these. */
static SceKernelThreadInfo g_hitch_ti;
static int hitch_thread_info(SceKernelThreadInfo *ti) {
    memset(ti, 0, sizeof *ti);
    ti->size = sizeof *ti;
    return sceKernelGetThreadInfo(sceKernelGetThreadId(), ti) >= 0;
}
extern void vita_heap_main_take(double *sum_ms, double *max_ms, int *calls);
#endif

void hitch_begin(void) {
    g_hitch_t0 = SDL_GetPerformanceCounter();
    g_hitch_n = 0;
#ifdef HITCH_THREAD
    if (!hitch_thread_info(&g_hitch_ti)) g_hitch_ti.size = 0;
    { double s, m; int c; vita_heap_main_take(&s, &m, &c); }   /* the span's own heap time from here */
#endif
}

void hitch_mark(const char *label) {
    if (g_hitch_n < 0 || g_hitch_n >= HITCH_MAX) return;
    g_hitch[g_hitch_n].label = label;
    g_hitch[g_hitch_n].t = SDL_GetPerformanceCounter();
    g_hitch_n++;
}

void hitch_report(double threshold_ms) {
    if (g_hitch_n < 0) return;
    double f = 1000.0 / (double)SDL_GetPerformanceFrequency();
    Uint64 end = SDL_GetPerformanceCounter();
    double total = (double)(end - g_hitch_t0) * f;
    double tpre = 0, tintr = 0, twait = 0, trun = 0, heap = 0, heap_max = 0;
    int heap_calls = 0;
#ifdef HITCH_THREAD
    {
        SceKernelThreadInfo ti;
        if (g_hitch_ti.size && hitch_thread_info(&ti)) {
            tpre = (double)(ti.threadPreemptCount - g_hitch_ti.threadPreemptCount);
            tintr = (double)(ti.intrPreemptCount - g_hitch_ti.intrPreemptCount);
            twait = (double)(ti.threadReleaseCount - g_hitch_ti.threadReleaseCount);
            trun = (double)(ti.runClocks - g_hitch_ti.runClocks) / 1000.0;
        }
        vita_heap_main_take(&heap, &heap_max, &heap_calls);
    }
    /* in a race a long update is worth a line from 20 ms (at most 2 a second) */
    {
        static Uint64 last_line;
        Uint64 now = SDL_GetPerformanceCounter();
        if (threshold_ms > 20.0 && total >= 20.0 && total < threshold_ms &&
            (double)(now - last_line) * f > 500.0) { threshold_ms = 20.0; last_line = now; }
    }
#endif
    if (total >= threshold_ms) {
        char line[1024];
        int len = snprintf(line, sizeof line, "hitch %.1f ms:", total);
        Uint64 prev = g_hitch_t0;
        for (int i = 0; i < g_hitch_n && len < (int)sizeof line - 48; i++) {
            double ms = (double)(g_hitch[i].t - prev) * f;
            prev = g_hitch[i].t;
            if (ms >= 1.0)
                len += snprintf(line + len, sizeof line - (size_t)len, " %s=%.1f",
                                g_hitch[i].label, ms);
        }
        double rest = (double)(end - prev) * f;
        if (rest >= 1.0 && len < (int)sizeof line - 32)
            len += snprintf(line + len, sizeof line - (size_t)len, " rest=%.1f", rest);
#ifdef HITCH_THREAD
        if (len < (int)sizeof line - 120)
            snprintf(line + len, sizeof line - (size_t)len,
                     " | main thread: preempted %.0f, interrupts %.0f, waits %.0f, ran %.1f ms; heap %.2f ms (%d calls, max %.2f)",
                     tpre, tintr, twait, trun, heap, heap_calls, heap_max);
#endif
        printf("%s\n", line);
    }
    /* rolling split of the frame's update: per label ms/frame (a label hit
       twice in a frame, e.g. once per sim tick, adds up), logged every 300
       frames */
    {
        enum { SPLIT_MAX = 160, SPLIT_FRAMES = 300 };
        static const char *s_label[SPLIT_MAX];
        static double s_sum[SPLIT_MAX], s_max[SPLIT_MAX], s_total, s_total_max;
        static double s_pre, s_pre_max, s_wait, s_wait_max, s_heap, s_heap_max, s_heap1;
        static int s_n, s_frames;
        double fr[SPLIT_MAX] = {0};
        Uint64 prev = g_hitch_t0;
        for (int i = 0; i < g_hitch_n; i++) {
            double ms = (double)(g_hitch[i].t - prev) * f;
            prev = g_hitch[i].t;
            int k = 0;
            while (k < s_n && s_label[k] != g_hitch[i].label) k++;
            if (k == s_n) { if (s_n == SPLIT_MAX) continue; s_label[s_n++] = g_hitch[i].label; }
            fr[k] += ms;
        }
        for (int k = 0; k < s_n; k++) {
            s_sum[k] += fr[k];
            if (fr[k] > s_max[k]) s_max[k] = fr[k];
        }
        s_total += total;
        if (total > s_total_max) s_total_max = total;
        s_pre += tpre + tintr; if (tpre + tintr > s_pre_max) s_pre_max = tpre + tintr;
        s_wait += twait; if (twait > s_wait_max) s_wait_max = twait;
        s_heap += heap; if (heap > s_heap_max) s_heap_max = heap;
        if (heap_max > s_heap1) s_heap1 = heap_max;
        if (++s_frames >= SPLIT_FRAMES) {
            char line[2048];
            int len = snprintf(line, sizeof line, "sim split ms/frame over %d frames: total %.1f (max %.1f)",
                               s_frames, s_total / s_frames, s_total_max);
            for (int k = 0; k < s_n && len < (int)sizeof line - 48; k++)
                if (s_sum[k] / s_frames >= 0.005 || s_max[k] >= 5.0)
                    len += snprintf(line + len, sizeof line - (size_t)len, " %s=%.3f/%.1f",
                                    s_label[k], s_sum[k] / s_frames, s_max[k]);
#ifdef HITCH_THREAD
            if (len < (int)sizeof line - 140)
                snprintf(line + len, sizeof line - (size_t)len,
                         " | main thread per frame: preempted %.2f (max %.0f), waits %.2f (max %.0f), heap %.3f ms (max %.2f, one call %.2f)",
                         s_pre / s_frames, s_pre_max, s_wait / s_frames, s_wait_max, s_heap / s_frames, s_heap_max, s_heap1);
#endif
            printf("%s\n", line);
            s_pre = s_pre_max = s_wait = s_wait_max = s_heap = s_heap_max = s_heap1 = 0;
            for (int k = 0; k < s_n; k++) s_sum[k] = s_max[k] = 0;
            s_total = s_total_max = 0;
            s_frames = 0;
        }
    }
    g_hitch_n = -1;
}

typedef struct { uint32_t key; int index; } ResidentTexSlot;
static int resident_tex_slot_cmp(const void *a, const void *b) {
    const ResidentTexSlot *p = (const ResidentTexSlot *)a, *q = (const ResidentTexSlot *)b;
    if (p->key != q->key) return p->key < q->key ? -1 : 1;
    return (p->index > q->index) - (p->index < q->index);   /* first slot wins, as before */
}

static int resident_resources_step(WorldResidentResources *resources,
                                   WorldNeighborhood *neighborhood,
                                   int max_batches,
                                   int include_vista,
                                   WResidentBuildTiming *timing) {
    if (!resources || !neighborhood || neighborhood->scene.count <= 0 ||
        !neighborhood->scene.meshes || !neighborhood->mbb) return -1;
    struct timespec rt0, rt1;
    int mesh_count = neighborhood->scene.count;
    int texture_cap = mesh_count + neighborhood->vista.count +
                      (neighborhood->nlights > 0 ? 1 : 0);

    if (!resources->mesh_count) {
        while (glGetError() != GL_NO_ERROR) {}
        resources->texture_keys = (uint32_t *)calloc(
            (size_t)texture_cap, sizeof *resources->texture_keys);
        resources->textures = (GLuint *)calloc(
            (size_t)texture_cap, sizeof *resources->textures);
        resources->texture_modes = (unsigned char *)calloc(
            (size_t)texture_cap, sizeof *resources->texture_modes);
        resources->mesh_textures = (GLuint *)calloc(
            (size_t)mesh_count, sizeof *resources->mesh_textures);
        resources->mesh_modes = (unsigned char *)calloc(
            (size_t)mesh_count, sizeof *resources->mesh_modes);
        resources->mesh_batch = (int *)malloc(
            (size_t)mesh_count * sizeof *resources->mesh_batch);
        if (!resources->texture_keys || !resources->textures ||
            !resources->texture_modes || !resources->mesh_textures ||
            !resources->mesh_modes || !resources->mesh_batch) goto fail;
        resources->mesh_count = mesh_count;
    }

    /* Setup stages, one frame each when sliced: 0 texture binding, 1 mesh ->
       texture lookup, 2 grass + sky/glow + batch plan, 3 batch uploads. They
       used to share one frame (a 45-60 ms hitch per streaming build). */
    int sliced = max_batches != INT_MAX;
    if (resources->setup_stage == 0) {
        Uint64 texture_begin = SDL_GetPerformanceCounter();
        World facade;
        memset(&facade, 0, sizeof facade);
        facade.neighborhood = *neighborhood;
        /* ponytail: eight uncached attempts, not a time deadline; split an
         * individual decode/upload only if one texture is a measured stall. */
        int bound;
#ifdef __vita__
        if (sliced && max_batches < 128) {
            /* smooth: up to 8 uploads, one at a time, until 4 ms are gone
               (hwlog29: 8 at once reached 9.5 ms with large textures) */
            Uint64 limit = SDL_GetPerformanceFrequency() * 4 / 1000;
            int k = 0;
            do bound = world_bind_textures_step(
                    &facade, resources->texture_keys, resources->textures,
                    resources->texture_modes, texture_cap, &resources->texture_binding, 1);
            while (!bound && ++k < 8 && SDL_GetPerformanceCounter() - texture_begin < limit);
        } else
#endif
        bound = world_bind_textures_step(
            &facade, resources->texture_keys, resources->textures,
            resources->texture_modes, texture_cap, &resources->texture_binding,
            max_batches == INT_MAX ? INT_MAX :
#ifdef __vita__
            16);   /* urgent (decoded on the worker; upload only) */
#else
            8);
#endif
        *neighborhood = facade.neighborhood;
        resources->texture_ticks += SDL_GetPerformanceCounter() - texture_begin;
        hitch_mark("fin.tex_bind");
        if (bound < 0) goto fail;
        if (!bound) return 0;
        resources->texture_count = resources->texture_binding.count;
        for (int i = 0; i < resources->texture_count; i++)
            if (!resources->textures[i]) goto fail;
        resources->setup_stage = 1;
        if (sliced) return 0;
    }

    if (resources->setup_stage == 1) {
        Uint64 texture_begin = SDL_GetPerformanceCounter();
        /* key -> first texture slot through a sorted index: the linear scan
           per mesh was meshes x textures (~2.5M compares) in one frame. */
        int nt = resources->texture_count;
        ResidentTexSlot *slots = (ResidentTexSlot *)malloc(
            (size_t)(nt ? nt : 1) * sizeof *slots);
        if (!slots) goto fail;
        for (int j = 0; j < nt; j++) {
            slots[j].key = resources->texture_keys[j];
            slots[j].index = j;
        }
        qsort(slots, (size_t)nt, sizeof *slots, resident_tex_slot_cmp);
        /* The worker's per-mesh record (mesh_tex) stands in for the mesh
           array, and neighbours mostly share a key: the lookup no longer
           walks ~15k mesh records on this thread. When every mesh matches
           the plan's assumption the plan binds without revalidating. */
        const ResidentMeshTex *mt = resources->mesh_tex &&
            resources->mesh_tex_n == mesh_count ? resources->mesh_tex : NULL;
        int ok = mt != NULL, lastj = -1, have_last = 0, nfu = 0, nmd = 0, nrf = 0;
        uint32_t lastkey = 0;
        for (int i = 0; i < mesh_count; i++) {
            resources->mesh_batch[i] = -1;
            uint32_t key = mt ? mt[i].key : neighborhood->scene.meshes[i].texkey;
            int j;
            if (have_last && key == lastkey) j = lastj;
            else {
                int lo = 0, hi = nt;
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    if (slots[mid].key < key) lo = mid + 1; else hi = mid;
                }
                j = lo < nt && slots[lo].key == key ? slots[lo].index : -1;
                lastkey = key; lastj = j; have_last = 1;
            }
            /* What the plan needs (upload_world_batches_bind_prepared): each
               mesh's effective mode as planned, and one texture per planned
               run. Runs are grouped by the worker's key, or by "unresolved"
               (0, or terrain): a key the lookup misses gives all its meshes
               0 -- still one texture -- unless a terrain mesh shares it (that
               one gets the grass); a key the worker missed but the lookup
               found splits the "unresolved" run. */
            if (j >= 0) {
                unsigned char tm = resources->texture_modes[j];
                resources->mesh_textures[i] = resources->textures[j];
                if (mt && mt[i].resolved && mt[i].raw == tm)
                    resources->mesh_modes[i] = mt[i].mode;
                else {
                    resources->mesh_modes[i] = (unsigned char)n2_world_draw_mode(
                        &neighborhood->scene.meshes[i], tm);
                    if (!mt) ok = 0;
                    else if (mt[i].pad & 2) {}   /* not planned */
                    else if (!mt[i].resolved) { ok = 0; nfu++; }
                    else if (resources->mesh_modes[i] != mt[i].mode) { ok = 0; nmd++; }
                }
            } else if (mt && !(mt[i].pad & 2) &&
                       (mt[i].mode != N2_DRAW_OPAQUE || (mt[i].resolved && (mt[i].pad & 1)))) {
                ok = 0; nrf++;
            }
        }
        free(slots);
        resources->plan_checked = ok;
        {
            static unsigned looks, fast;
            looks++; fast += ok != 0;
            if ((looks & 15) == 1 || !ok)
                printf("resident: tex lookup %s (%u of %u builds matched the worker plan;"
                       " %d found/unplanned, %d mode, %d planned/missing)\n",
                       ok ? "matched the plan" : mt ? "differs from the plan" : "without a plan",
                       fast, looks, nfu, nmd, nrf);
        }
        free(resources->mesh_tex); resources->mesh_tex = NULL; resources->mesh_tex_n = 0;
        resources->texture_ticks += SDL_GetPerformanceCounter() - texture_begin;
        hitch_mark("fin.tex_lookup");
        resources->setup_stage = 2;
        if (sliced) return 0;
    }

    /* hwlog28: grass + sky/glow + plan in one frame were 15-20 ms, and the
       upload's tail (sorting, normals, vista) as much again -- the frames
       that set off the 3-tick catch-up. Unhurried (smooth) finishes take
       them one frame each; the urgent ones keep going. */
    int smooth = sliced && max_batches < 128;
    if (resources->setup_stage == 2) {
        Uint64 texture_begin = SDL_GetPerformanceCounter();
        if (neighborhood->have_grass)
            resources->terrain_texture = upload_tex(&neighborhood->grass);
        hitch_mark("fin.grass");
        resources->texture_ticks += SDL_GetPerformanceCounter() - texture_begin;
        if (timing) timing->textures_ms = (uint32_t)(
            resources->texture_ticks * 1000 / SDL_GetPerformanceFrequency());
        VMARK("  resident: %d textures uploaded", resources->texture_count);
        resources->setup_stage = 7;
        if (smooth) return 0;
    }
    if (resources->setup_stage == 7) {
        Uint64 batch_begin = SDL_GetPerformanceCounter();
        resources->sky_count = upload_cat_batches(
            &neighborhood->scene, N2_SKY, resources->mesh_textures,
            &resources->sky, NULL);
        if (resources->sky_count < 0) goto fail;
        hitch_mark("fin.sky");
        resources->batch_ticks += SDL_GetPerformanceCounter() - batch_begin;
        resources->setup_stage = 10;
        if (smooth) return 0;   /* hwlog29: sky + glow together reached 9 ms */
    }
    if (resources->setup_stage == 10) {
        Uint64 batch_begin = SDL_GetPerformanceCounter();
        resources->glow_count = upload_cat_batches(
            &neighborhood->scene, N2_GLOW, resources->mesh_textures,
            &resources->glow, NULL);
        if (resources->sky_count < 0 || resources->glow_count < 0) goto fail;
        hitch_mark("fin.sky_glow");
        resources->batch_ticks += SDL_GetPerformanceCounter() - batch_begin;
        resources->setup_stage = 8;
        if (smooth) return 0;
    }
    if (resources->setup_stage == 8) {
        Uint64 batch_begin = SDL_GetPerformanceCounter();
        if (resources->cpu_plan) {
            if ((resources->plan_checked ? upload_world_batches_bind_checked :
                 upload_world_batches_bind_prepared)(resources->cpu_plan,
                    resources->mesh_textures,resources->terrain_texture,resources->mesh_modes)) {
                resources->upload=resources->cpu_plan; resources->cpu_plan=NULL;
            } else {
                printf("worker batch plan: rejected texture/material mismatch; regrouping\n");
                upload_world_batches_cancel(&resources->cpu_plan);
            }
        }
        if (!resources->upload) resources->upload = upload_world_batches_begin(
            &neighborhood->scene, (const float (*)[4])neighborhood->mbb,
            resources->mesh_textures, resources->terrain_texture,
            NULL, resources->mesh_modes);
        if (!resources->upload) goto fail;
        upload_world_batches_set_normals(resources->upload, resources->mesh_normals);
        upload_world_batches_set_packed(resources->upload, resources->mesh_packed);
        hitch_mark("fin.plan");
        VMARK("  resident: sky %d, glow %d batches; world batch upload begins",
              resources->sky_count, resources->glow_count);
        resources->batch_ticks += SDL_GetPerformanceCounter() - batch_begin;
        resources->setup_stage = 3;
        return 0; /* keep texture/partition work separate from ordinary uploads */
    }

    Uint64 batch_begin = SDL_GetPerformanceCounter();
    if (resources->setup_stage == 3) {
        hitch_mark("fin.pre_upload");
        int uploaded = upload_world_batches_step(&resources->upload, max_batches,
                            &resources->ordinary, &resources->ordinary_count,
                            resources->mesh_batch);
        if (uploaded < 0) goto fail;
        if (!uploaded) {
            resources->batch_ticks += SDL_GetPerformanceCounter() - batch_begin;
            return 0;
        }
        if (resources->ordinary_count <= 0 || !resources->ordinary) goto fail;
        hitch_mark("fin.batch_upload");
        VMARK("  resident: %d world batches uploaded", resources->ordinary_count);
        hitch_mark("fin.vmark_batches");
        resources->setup_stage = 9;
        if (smooth) { resources->batch_ticks += SDL_GetPerformanceCounter() - batch_begin; return 0; }
    }
#ifdef __vita__
    if (!resident_defer_normals(resources))
#endif
    resident_free_normals(resources);
    hitch_mark("fin.free_normals");
    /* only the meshes the worker saw holding a shared packed copy (hwlog51_L:
       walking all ~15k mesh records was ~3 ms of a finish frame) */
    render_world_packed_trim_list(&neighborhood->scene, resources->trim_list,
                                  resources->trim_n);
    free(resources->trim_list); resources->trim_list = NULL; resources->trim_n = 0;
    {   /* sim check: what a whole-scene walk would still have freed */
        static int verify = -1;
        if (verify < 0) { const char *e = getenv("OPENUG2_ASM_VERIFY"); verify = e && e[0] == '1'; }
        if (verify) {
            int left = 0;
            const N2Scene *sc = &neighborhood->scene;
            for (int i = 0; i < sc->count; i++) {
                const N2SharedGeometry *g = sc->meshes[i].shared_geometry;
                if (g && g->packed && g->packed_hashed && !sc->meshes[i].fade_in) {
                    if (left < 3) printf("trim verify: left mesh %d role %d cat %d nverts %d\n", i,
                        sc->meshes[i].world_role, sc->meshes[i].cat, sc->meshes[i].nverts);
                    left++;
                }
            }
            printf("trim verify: %d copies a full walk would still free\n", left);
        }
    }
    hitch_mark("fin.packed_trim");
    if (include_vista && !resident_vista_batches(resources, neighborhood)) goto fail;
    hitch_mark("fin.vista");

    resources->debug_batches = (WorldMeshBatch *)calloc(
        (size_t)resources->ordinary_count, sizeof *resources->debug_batches);
    if (!resources->debug_batches) goto fail;
    for (int i = 0; i < resources->ordinary_count; i++) {
        resources->debug_batches[i].vbo = resources->ordinary[i].vbo;
        resources->debug_batches[i].ibo = resources->ordinary[i].ibo;
        resources->debug_batches[i].index_count =
            (uint32_t)resources->ordinary[i].index_count;
        resources->debug_batches[i].chunk_id = (uint32_t)i;
    }
    resources->batch_ticks += SDL_GetPerformanceCounter() - batch_begin;
    hitch_mark("fin.debug_batches");
    if (timing) {
        timing->batches_ms = (uint32_t)(resources->batch_ticks * 1000 /
                                       SDL_GetPerformanceFrequency());
        clock_gettime(CLOCK_MONOTONIC, &rt0);
    }

    if (resources->obstacles) {   /* collected by the worker (Vita) */
        VMARK("  resident: %d collision obstacles (worker)", resources->obstacle_count);
        if (glGetError() != GL_NO_ERROR) goto fail;
        return 1;
    }
    int obstacle_cap = mesh_count;
    resources->obstacles = (float (*)[4])calloc(
        (size_t)obstacle_cap, sizeof *resources->obstacles);
    resources->obstacle_z = (float (*)[2])calloc(
        (size_t)obstacle_cap, sizeof *resources->obstacle_z);
    resources->obstacle_src = (int *)calloc(
        (size_t)obstacle_cap, sizeof *resources->obstacle_src);
    if (!resources->obstacles || !resources->obstacle_z ||
        !resources->obstacle_src) goto fail;
    resources->obstacle_count = phys_collect_walls(
        &neighborhood->scene, resources->obstacles,
        resources->obstacle_src, resources->obstacle_z, obstacle_cap);
    if (timing) { clock_gettime(CLOCK_MONOTONIC, &rt1);
                  timing->collision_ms = wrb_ms(&rt0, &rt1); }
    VMARK("  resident: %d collision obstacles", resources->obstacle_count);
#if 0
    {   /* CPU mesh memory by role: what streaming could drop after upload */
        const N2Scene *sc = &neighborhood->scene;
        unsigned char *wall = (unsigned char *)calloc((size_t)sc->count + 1, 1);
        long bytes[3] = {0, 0, 0};   /* ground, wall source, render-only */
        for (int i = 0; wall && i < resources->obstacle_count; i++) {
            int m = resources->obstacle_src[i];
            if (m >= 0 && m < sc->count) wall[m] = 1;
        }
        for (int i = 0; wall && i < sc->count; i++) {
            const N2Mesh *m = &sc->meshes[i];
            long b = (long)m->nverts * 5 * 4 + (long)m->nidx * 2;
            int k = (m->cat == N2_ROAD || m->cat == N2_TERRAIN) ? 0 : wall[i] ? 1 : 2;
            bytes[k] += b;
        }
        free(wall);
        VMARK("  mesh memory: ground %ld MB, walls %ld MB, render-only %ld MB, mesh records %ld MB",
              bytes[0] >> 20, bytes[1] >> 20, bytes[2] >> 20,
              ((long)sc->count * (long)sizeof(N2Mesh)) >> 20);
    }
#endif
    if (glGetError() != GL_NO_ERROR) goto fail;
    return 1;

fail:
    world_resident_resources_free(resources);
    return -1;
}

int world_resident_resources_build(WorldResidentResources *resources,
                                   WorldNeighborhood *neighborhood,
                                   int include_vista,
                                   WResidentBuildTiming *timing) {
    world_resident_resources_free(resources);
    int status;
    do { status = resident_resources_step(resources, neighborhood, INT_MAX, include_vista, timing); }
    while (!status);
    return status > 0;
}

static int resident_validate_meshes(const WorldResident *resident);

int world_resident_validate_cpu(const WorldResident *resident,
                                float player_x, float player_y, float player_z) {
    if (!resident || !isfinite(player_x) || !isfinite(player_y) ||
        !isfinite(player_z)) return 0;
    if (!resident->meshes_validated && !resident_validate_meshes(resident)) return 0;
    WGroundHit hit;
    return world_ground_hit_grid(&resident->world.grid, &resident->world.scene,
                                player_x, player_y, player_z, &hit) !=
           WSURF_NONE;
}

static int resident_validate_meshes(const WorldResident *resident) {
    const float player_x = 0, player_y = 0, player_z = 0;
    if (!resident || !isfinite(player_x) || !isfinite(player_y) ||
        !isfinite(player_z) || !isfinite(resident->center[0]) ||
        !isfinite(resident->center[1]) || !isfinite(resident->radius) ||
        resident->radius <= 0.0f) return 0;
    const WorldNeighborhood *world = &resident->world;
    const N2Scene *scene = &world->scene;
    if (!scene->meshes || scene->count <= 0 || !world->mbb ||
        !world->grid.start || !world->grid.list ||
        world->grid.gw <= 0 || world->grid.gh <= 0 ||
        world->grid.meshes != scene->meshes ||
        world->inst_stats.rejected_meshes != 0 ||
        world->center[0] != resident->center[0] ||
        world->center[1] != resident->center[1] ||
        world->radius != resident->radius) {
        printf("resident validate: setup mismatch (grid %s, rejected %d, centre/radius %s)\n",
               world->grid.meshes != scene->meshes ? "points at another mesh array" : "ok",
               (int)world->inst_stats.rejected_meshes,
               world->center[0] != resident->center[0] || world->center[1] != resident->center[1] ||
               world->radius != resident->radius ? "differ" : "ok");
        return 0;
    }

    int ground_meshes = 0;
    for (int i = 0; i < scene->count; i++) {
        const N2Mesh *mesh = &scene->meshes[i];
        if (!mesh->verts || !mesh->idx || mesh->nverts <= 0 || mesh->nidx <= 0)
            return 0;
        if (mesh->cat == N2_ROAD || mesh->cat == N2_TERRAIN) ground_meshes++;
        if (mesh->world_bounds_valid) {
            for (int k = 0; k < 6; k++) if (!isfinite(mesh->world_bounds[k])) return 0;
            for (int k = 0; k < 3; k++)
                if (mesh->world_bounds[k] > mesh->world_bounds[k+3]) return 0;
        } else {
            for (int j = 0; j < mesh->nverts; j++)
                for (int k = 0; k < 3; k++)
                    if (!isfinite(mesh->verts[j*5+k])) return 0;
        }
        for (int j = 0; j < mesh->nidx; j++)
            if (mesh->idx[j] >= mesh->nverts) return 0;
        for (int j = 0; j < 4; j++)
            if (!isfinite(world->mbb[i][j])) return 0;
    }
    if (!ground_meshes) printf("resident validate: no ground meshes\n");
    return ground_meshes > 0;
}

int world_resident_prepare(WorldResident *candidate,
                           const WResidentBuildArgs *args,
                           float center_x, float center_y,
                           WResidentBuildTiming *timing) {
    if (!candidate || !args || !args->track_root || !args->trackname ||
        !world_resident_policy_valid(&args->policy) ||
        !isfinite(center_x) || !isfinite(center_y)) return 0;
    if (timing) memset(timing, 0, sizeof *timing);
    candidate->center[0] = center_x;
    candidate->center[1] = center_y;
    candidate->radius = args->policy.resident_radius;
    candidate->omit_vista = args->omit_vista;
    struct timespec t0, t1;
    if (timing) clock_gettime(CLOCK_MONOTONIC, &t0);

    WLoadOptions options = {
        1, center_x, center_y, args->policy.resident_radius,
        args->scenery_event
    };
    if (!world_neighborhood_load(&candidate->world, args->track_root,
                                 args->trackname, &options)) return 0;
    for (int i = 0; i < candidate->world.scene.count; i++)
        if (candidate->world.scene.meshes[i].cat == N2_SKY)
            candidate->world.scene.meshes[i].texkey = n2_sky_remap_key(
                args->sky_profile, candidate->world.scene.meshes[i].texkey);
    if (timing) { clock_gettime(CLOCK_MONOTONIC, &t1);
                  timing->neighborhood_ms = wrb_ms(&t0, &t1); }
    return 1;
}

int world_resident_finish(WorldResident *candidate, float x, float y, float z,
                          WResidentBuildTiming *timing) {
    struct timespec t0, t1;
    if (timing) clock_gettime(CLOCK_MONOTONIC, &t0);
    if (!world_resident_validate_cpu(candidate, x, y, z)) return 0;
    if (timing) { clock_gettime(CLOCK_MONOTONIC, &t1);
                  timing->validate_ms = wrb_ms(&t0, &t1); t0 = t1; }

    if (!world_resident_resources_build(&candidate->resources,
                                        &candidate->world, !candidate->omit_vista, timing)) return 0;
    if (timing) timing->total_ms = timing->neighborhood_ms +
                    timing->validate_ms + timing->textures_ms +
                    timing->batches_ms + timing->collision_ms;
    return 1;
}

int world_resident_finish_step(WorldResident *candidate, float x, float y, float z,
                               int max_batches, WResidentBuildTiming *timing) {
    if (!candidate || max_batches <= 0) return -1;
    if (!candidate->resources.mesh_count) {
        Uint64 begin = SDL_GetPerformanceCounter();
        if (!world_resident_validate_cpu(candidate, x, y, z)) return -1;
        if (timing) timing->validate_ms = (uint32_t)(
            (SDL_GetPerformanceCounter() - begin) * 1000 / SDL_GetPerformanceFrequency());
    }
    hitch_mark("fin.validate");
    int status = resident_resources_step(&candidate->resources, &candidate->world,
                                         max_batches, !candidate->omit_vista, timing);
    if (status > 0) {
        WGroundHit hit;
        if (!isfinite(x) || !isfinite(y) || !isfinite(z) ||
            world_ground_hit_grid(&candidate->world.grid, &candidate->world.scene,
                                  x, y, z, &hit) == WSURF_NONE)
            return -1;
        hitch_mark("fin.ground_check");
        if (timing) timing->total_ms = timing->neighborhood_ms + timing->validate_ms +
                timing->textures_ms + timing->batches_ms + timing->collision_ms;
    }
    return status;
}

int world_resident_build(WorldResident *candidate,
                         const WResidentBuildArgs *args,
                         float center_x, float center_y,
                         float player_x, float player_y, float player_z,
                         WResidentBuildTiming *timing) {
    if (!candidate) return 0;
    memset(candidate, 0, sizeof *candidate);
    if (world_resident_prepare(candidate, args, center_x, center_y, timing) &&
        world_resident_finish(candidate, player_x, player_y, player_z, timing))
        return 1;
    world_resident_resources_free(&candidate->resources);
    world_neighborhood_free(&candidate->world);
    memset(candidate, 0, sizeof *candidate);
    return 0;
}

struct WResidentJob {
    SDL_Thread *thread;
    SDL_atomic_t done;
    WResidentBuildArgs args;
    char *root, *track;
    WorldResident *candidate;
    WResidentBuildTiming timing;
    int ok;
    int aborted;
    Uint32 start_ms;
};

static SDL_atomic_t g_resident_abort;
int world_build_aborted(void) { return SDL_AtomicGet(&g_resident_abort); }

/* Loader-only parser scratch is shared within each translation unit. Keep
 * ownership until join, including completed-but-not-yet-consumed requests. */
static SDL_atomic_t resident_loader_busy;
/* a streaming build is on the worker (reading tiles and textures) */
int world_resident_loader_busy(void) { return SDL_AtomicGet(&resident_loader_busy); }
/* the worker is reading textures from the card right now */
int world_resident_texture_fetch(void) { return SDL_AtomicGet(&resident_tex_fetch); }

#ifdef __vita__
/* Where a freshly prepared resident's CPU memory goes (loader thread, after
   precompute). "own" bytes belong to this resident alone; "shared" bytes are
   also referenced by the placement cache or the previous resident. */
static void resident_memory_report(const WorldResident *r) {
    enum { GROUND, WALL, COLL, STANDIN, DRAWN, VISTA, NCAT };
    static const char *const name[NCAT] = { "ground", "walls", "collision-only",
                                            "stand-ins", "drawn", "vista" };
    long own[NCAT] = {0}, shared[NCAT] = {0}, vcol_bytes = 0;
    const N2Scene *sc = &r->world.scene;
    const WorldResidentResources *res = &r->resources;
    unsigned char *wall = (unsigned char *)calloc((size_t)sc->count + 1, 1);
    for (int i = 0; wall && i < res->obstacle_count; i++) {
        int m = res->obstacle_src[i];
        if (m >= 0 && m < sc->count) wall[m] = 1;
    }
    for (int pass = 0; pass < 2; pass++) {
        const N2Scene *s = pass ? &r->world.vista : sc;
        for (int i = 0; i < s->count; i++) {
            const N2Mesh *m = &s->meshes[i];
            long b = (long)m->nverts * (m->authored_normals ? 8 : 5) * 4 + (long)m->nidx * 2;
            if (m->vcol) { b += (long)m->nverts * 4; vcol_bytes += (long)m->nverts * 4; }
            int k = pass ? VISTA
                  : (m->cat == N2_ROAD || m->cat == N2_TERRAIN) ? GROUND
                  : m->world_role == 1 ? COLL : m->world_role == 2 ? STANDIN
                  : (wall && wall[i]) ? WALL : DRAWN;
            if (m->shared_geometry) shared[k] += b; else own[k] += b;
        }
    }
    free(wall);
    long normals = 0, packed_own = 0, packed_borrowed = 0;
    for (int i = 0; res->mesh_normals && i < res->normals_count; i++)
        if (res->mesh_normals[i]) normals += (long)sc->meshes[i].nverts * 12;
    for (int i = 0; res->mesh_packed && i < sc->count; i++) {
        const PackedMesh *pk = &res->mesh_packed[i];
        if (!pk->v) continue;
        long b = (long)sc->meshes[i].nverts * (long)sizeof(BatchedVertex);
        if (pk->borrowed) packed_borrowed += b; else packed_own += b;
    }
    long records = (long)(sc->cap + r->world.vista.cap) * (long)sizeof(N2Mesh);
    extern size_t world_instance_cache_bytes(void);
    char line[400]; int n = 0;
    for (int k = 0; k < NCAT; k++)
        n += snprintf(line + n, sizeof line - (size_t)n, "%s %ld+%ld ", name[k],
                      own[k] >> 20, shared[k] >> 20);
    VMARK("  memory (MB own+shared): %s| vcol %ld | records %ld (%d meshes x %d B) | "
          "normals %ld | packed %ld own, %ld cached | lights %ld KB | walls %ld KB | placement cache %ld",
          line, vcol_bytes >> 20, records >> 20, sc->count, (int)sizeof(N2Mesh),
          normals >> 20, packed_own >> 20, packed_borrowed >> 20,
          (long)r->world.nlights * (long)sizeof(N2LightSrc) >> 10,
          (long)res->obstacle_count * 28 >> 10, (long)(world_instance_cache_bytes() >> 20));
    const WGroundGrid *g = &r->world.grid;
    long grid = 0;
    if (g->start) grid += ((long)g->gw * g->gh + 1) * 4 + (long)g->start[g->gw * g->gh] * 4;
    if (g->tstart) grid += ((long)g->tgw * g->tgh + 1) * 4 + (long)g->tstart[g->tgw * g->tgh] * 4;
    VMARK("  memory (MB): loc4 copy %ld, ads copy %ld, ground grid %ld, mbb %ld KB",
          r->world.loc4_shared ? 0 : r->world.loc4len >> 20, r->world.adslen >> 20, grid >> 20,
          (long)sc->count * 16 >> 10);
}
#endif

static int resident_worker(void *data) {
    WResidentJob *job = data;
#ifdef __vita__
    extern void vita_heap_report(const char *label);
    render_world_packed_trim_flush();
    resident_deferred_flush();
    vita_heap_report("at build start");
#endif
    job->ok = world_resident_prepare(job->candidate, &job->args,
                                     job->candidate->center[0],
                                     job->candidate->center[1], &job->timing);
#ifdef __vita__
    if (job->ok && world_build_aborted()) job->ok = 0;
    if (job->ok) job->ok = resident_worker_precompute(job->candidate);
    job->aborted = world_build_aborted();
    if (job->ok) resident_memory_report(job->candidate);
    vita_heap_report("at build end");
#endif
    /* SDL's atomic release/acquire publishes all candidate writes. No GL,
     * active-scene queries or partial-output cleanup occurs on this thread. */
    SDL_AtomicSet(&job->done, 1);
    return 0;
}

static void resident_job_free(WResidentJob *job) {
    free(job->root); free(job->track); free(job);
}

int world_resident_job_start(WResidentJob **slot, const WResidentBuildArgs *args,
                             float center_x, float center_y) {
    if (!slot || *slot || !args || !args->track_root || !args->trackname ||
        !world_resident_policy_valid(&args->policy) ||
        !isfinite(center_x) || !isfinite(center_y)) return 0;
    WResidentJob *job = calloc(1, sizeof *job);
    if (!job) return 0;
    job->root = malloc(strlen(args->track_root) + 1);
    job->track = malloc(strlen(args->trackname) + 1);
    job->candidate = calloc(1, sizeof *job->candidate);
    if (!job->root || !job->track || !job->candidate) {
        free(job->candidate); resident_job_free(job); return 0;
    }
    strcpy(job->root, args->track_root);
    strcpy(job->track, args->trackname);
    job->args = *args;
    job->args.track_root = job->root;
    job->args.trackname = job->track;
    job->candidate->center[0] = center_x;
    job->candidate->center[1] = center_y;
    SDL_AtomicSet(&job->done, 0);
    job->start_ms = SDL_GetTicks();
    if (!SDL_AtomicCAS(&resident_loader_busy, 0, 1)) {
        free(job->candidate); resident_job_free(job); return 0;
    }
    SDL_AtomicSet(&g_resident_abort, 0);
    job->thread = SDL_CreateThread(resident_worker, "world-prepare", job);
    if (!job->thread) {
        SDL_AtomicSet(&resident_loader_busy, 0);
        free(job->candidate); resident_job_free(job); return 0;
    }
    *slot = job;
    return 1;
}

int world_resident_job_take(WResidentJob **slot, WorldResident **candidate,
                            WResidentBuildTiming *timing) {
    if (!slot || !*slot || !candidate || !SDL_AtomicGet(&(*slot)->done)) return 0;
    WResidentJob *job = *slot;
    SDL_WaitThread(job->thread, NULL);
    int result;
    if (job->aborted) {         /* asked to stop: not a failed cell */
        printf("resident build aborted at (%.0f,%.0f) after %u ms\n",
               job->candidate->center[0], job->candidate->center[1],
               SDL_GetTicks() - job->start_ms);
        world_resident_free(job->candidate);
        result = 0;
    } else {
        *candidate = job->candidate;
        if (timing) *timing = job->timing;
        result = job->ok ? 1 : -1;
    }
    SDL_AtomicSet(&g_resident_abort, 0);
    resident_job_free(job);
    SDL_AtomicSet(&resident_loader_busy, 0);
    *slot = NULL;
    return result;
}

int world_resident_job_abort(WResidentJob *job) {
    if (!job || SDL_AtomicGet(&job->done)) return 0;
    return SDL_AtomicCAS(&g_resident_abort, 0, 1) ? 1 : 0;
}

int world_resident_job_info(const WResidentJob *job, float center[2],
                            unsigned *age_ms) {
    if (!job) return 0;
    center[0] = job->candidate->center[0];
    center[1] = job->candidate->center[1];
    *age_ms = SDL_GetTicks() - job->start_ms;
    return 1;
}

void world_resident_job_cancel(WResidentJob **slot) {
    if (!slot || !*slot) return;
    WResidentJob *job = *slot;
    SDL_WaitThread(job->thread, NULL);
    world_resident_free(job->candidate);
    resident_job_free(job);
    /* an abort asked of this job must not stop the next prepare (the track
       switch's runs outside a job) */
    SDL_AtomicSet(&g_resident_abort, 0);
    SDL_AtomicSet(&resident_loader_busy, 0);
    *slot = NULL;
}

void world_resident_activate(WorldResident **active,
                             WorldResident **candidate) {
    if (!active || !candidate || !*candidate) return;
    WorldResident *next = *candidate;
    WorldResident *previous = *active;
    next->generation = previous ? previous->generation + 1 : 1;
    *active = next;
    *candidate = previous;
    world_ground_grid_activate(&next->world.grid);
}

void world_resident_free(WorldResident *resident) {
    if (!resident) return;
    world_resident_resources_free(&resident->resources);
    world_neighborhood_free(&resident->world);
    free(resident);
}

/* PERF_PLAN P1a (11-14): a swapped-out area's meshes let go of their tile
   geometry on a thread of their own, the moment the swap happens. The retire
   step released them 256 a frame on the main thread (~3 s for a downtown
   area of 18k meshes), so with a build every ~3 s at speed two or three old
   areas held their tiles at once: 40-64 MB of geometry "held by older areas"
   on top of the ~50 MB tile cache (sim route downtown55), and the Vita ran
   out (hwlog55). The batches (GL) still retire on the main thread. */
typedef struct RelItem { struct RelItem *next; N2Mesh *m; int n; } RelItem;
static RelItem *volatile g_rel_head;
static volatile int g_rel_lock, g_rel_started, g_rel_busy;
static volatile long g_rel_meshes;
static void rel_lock(void) { while (__sync_lock_test_and_set(&g_rel_lock, 1)) SDL_Delay(0); }
static void rel_unlock(void) { __sync_lock_release(&g_rel_lock); }
static int rel_thread(void *unused) {
    (void)unused;
    for (;;) {
        rel_lock();
        RelItem *it = g_rel_head; g_rel_head = NULL;
        if (it) g_rel_busy = 1;
        rel_unlock();
        if (!it) { SDL_Delay(15); continue; }
        while (it) {
            RelItem *nx = it->next;
            for (int i = 0; i < it->n; i++) n2_mesh_geometry_release(&it->m[i]);
            __sync_fetch_and_sub(&g_rel_meshes, (long)it->n);
            free(it->m); free(it);
            it = nx;
        }
        g_rel_busy = 0;
    }
    return 0;
}
int world_resident_release_meshes_async(WorldResident *r) {
    if (!r) return 0;
    if (!g_rel_started) {
        g_rel_started = 1;
        if (!SDL_CreateThread(rel_thread, "area-release", NULL)) g_rel_started = 2;
    }
    if (g_rel_started != 1) return 0;   /* no thread: the retire step does it */
    int moved = 0;
    N2Scene *scenes[] = { &r->world.scene, &r->world.vista };
    for (int g = 0; g < 2; g++) {
        N2Scene *sc = scenes[g];
        if (!sc->meshes || sc->count <= 0) continue;
        RelItem *it = (RelItem *)malloc(sizeof *it);
        if (!it) continue;
        it->m = sc->meshes; it->n = sc->count;
        sc->meshes = NULL; sc->count = 0; sc->cap = 0;
        __sync_fetch_and_add(&g_rel_meshes, (long)it->n);
        rel_lock(); it->next = g_rel_head; g_rel_head = it; rel_unlock();
        moved += it->n;
    }
    return moved;
}
long world_resident_release_pending(void) { return g_rel_meshes; }

int world_resident_retire_step(WorldResident **slot, int max_items) {
    if (!slot || !*slot) return 1;
    if (max_items <= 0) return 0;
    WorldResident *r = *slot;
    WorldResidentResources *v = &r->resources;
    N2Batch *batches[] = {v->ordinary, v->sky, v->glow, v->vista};
    int *counts[] = {&v->ordinary_count, &v->sky_count,
                    &v->glow_count, &v->vista_count};
    for (int g = 0; g < 4; g++) {
        if (!batches[g]) continue;
        while (*counts[g] > 0) {
            if (!max_items) return 0;
            N2Batch *b = &batches[g][--*counts[g]];
            render_batch_release(b);
            max_items--;
        }
    }
    hitch_mark("ret.batches");
    N2Scene *scenes[] = {&r->world.scene, &r->world.vista};
    for (int g = 0; g < 2; g++) {
        N2Scene *s = scenes[g];
        if (!s->meshes) continue;
        while (s->count > 0) {
            if (!max_items) return 0;
            N2Mesh *m = &s->meshes[--s->count];
            n2_mesh_geometry_release(m);
            max_items--;
        }
    }
    hitch_mark("ret.meshes");
    world_resident_free(r); /* emptied arrays and remaining small owners */
    hitch_mark("ret.free");
    *slot = NULL;
    return 1;
}
