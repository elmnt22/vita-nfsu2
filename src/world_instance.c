#include "world_instance.h"

#ifdef __vita__
#include "SDL.h"
void vita_mark(const char *fmt, ...);
#define VMARK(...) vita_mark(__VA_ARGS__)
#else
#define VMARK(...) ((void)0)
#endif

#include <limits.h>

/* world_scenery.h: the career stage whose scenery / lock walls are up (-1 none). */
int g_wg_career_stage = -1, g_wg_career_owns_car;
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nfsu2.h"
#include "world_scenery.h"
int world_build_aborted(void);

#define WINST_WORLD_LIMIT 1e8f
static int g_world_low_detail;
void world_instance_set_detail(int low) { g_world_low_detail = !!low; }
static int g_winst_convert;
void world_instance_set_convert(int on) { g_winst_convert = !!on; }

/* Prototype meshes are parsed lazily. The library scan records only what the
 * resolver and ownership passes need (identity, category flags, mesh count);
 * the geometry is re-parsed from [begin,end) the first time a placement uses
 * the prototype. A whole STREAM bundle's models are far larger than the
 * handful a view-radius build actually places. */
typedef struct {
    N2Scene scene;       /* empty until winst_proto_load */
    float matrix[16];
    uint32_t name_hash;
    uint32_t model_key;  /* authored 0x134011+0x10; display names may truncate */
    char name[28];
    unsigned char has_matrix;
    unsigned char is_vista;
    /* Set by the ownership pre-pass: some record places this prototype with a
     * real transform, so it is an instanced object and not a ground chunk. */
    unsigned char instanced;
    unsigned char loaded;
    unsigned char any_ground;   /* some mesh is ROAD/TERRAIN */
    unsigned char all_ground;   /* every mesh is ROAD/TERRAIN */
    int mesh_count;             /* meshes the full parse produces (> 0) */
    int index_count;
    long begin, end;            /* object payload range in the bundle */
} WInstProto;

typedef struct {
    WInstProto *items;
    int count, cap;
    const unsigned char *data;  /* bundle the ranges index; borrowed */
    const uint32_t *keys;       /* texture keys for re-parsing; borrowed */
    int nkeys;
    int *model_index; /* open-addressed slots: first prototype index + 1 */
    unsigned model_index_cap;
} WInstLibrary;

/* Kept out of the public header: the standalone regression uses this to prove
 * a rejected placement releases every copy allocated for that attempted mesh. */
static long winst_placement_live_allocations;

static void *winst_place_alloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) winst_placement_live_allocations++;
    return ptr;
}

static void winst_place_free(void *ptr) {
    if (ptr) {
        winst_placement_live_allocations--;
        free(ptr);
    }
}

long winst_test_placement_live_allocations(void) {
    return winst_placement_live_allocations;
}

static void winst_reject(WInstStats *stats) {
    if (stats) stats->rejected_meshes++;
}

static int winst_push_mesh(N2Scene *dst, N2Mesh mesh) {
    if (dst->count == dst->cap) {
        int next = dst->cap ? dst->cap * 2 : 64;
        if (next <= dst->cap || (size_t)next > SIZE_MAX / sizeof *dst->meshes)
            return 0;
        N2Mesh *grown = (N2Mesh *)realloc(dst->meshes,
                                           (size_t)next * sizeof *dst->meshes);
        if (!grown) return 0;
        dst->meshes = grown;
        dst->cap = next;
    }
    dst->meshes[dst->count++] = mesh;
    return 1;
}

int winst_place_mesh(N2Scene *dst, const N2Mesh *src,
                     const float matrix[16], const char *asset_name,
                     WInstStats *stats) {
    if (!dst || !src || !matrix || !src->verts || !src->idx ||
        src->nverts <= 0 || src->nidx <= 0) {
        winst_reject(stats);
        return 0;
    }
    size_t nverts = (size_t)src->nverts, nidx = (size_t)src->nidx;
    if (nverts > SIZE_MAX / (5 * sizeof *src->verts) ||
        nidx > SIZE_MAX / sizeof *src->idx ||
        (src->vcol && nverts > SIZE_MAX / (4 * sizeof *src->vcol))) {
        winst_reject(stats);
        return 0;
    }

    N2Mesh placed = *src;
    placed.world_bounds_valid = 0;
    for (int k = 0; k < 3; k++) {
        placed.world_bounds[k] = 1e30f;
        placed.world_bounds[k+3] = -1e30f;
    }
    placed.authored_normals = 0; /* world placement rebuilds the vertex pool */
    placed.verts = (float *)winst_place_alloc(nverts * 5 * sizeof *placed.verts);
    placed.idx = (uint16_t *)winst_place_alloc(nidx * sizeof *placed.idx);
    placed.vcol = src->vcol ? (unsigned char *)winst_place_alloc(nverts * 4) : NULL;
    if (!placed.verts || !placed.idx || (src->vcol && !placed.vcol)) {
        winst_place_free(placed.verts);
        winst_place_free(placed.idx);
        winst_place_free(placed.vcol);
        winst_reject(stats);
        return 0;
    }

    memcpy(placed.idx, src->idx, nidx * sizeof *placed.idx);
    if (placed.vcol) memcpy(placed.vcol, src->vcol, nverts * 4);
    for (int i = 0; i < src->nverts; i++) {
        const float *in = src->verts + i * 5;
        float *out = placed.verts + i * 5;
        out[0] = in[0] * matrix[0] + in[1] * matrix[4] + in[2] * matrix[8] + matrix[12];
        out[1] = in[0] * matrix[1] + in[1] * matrix[5] + in[2] * matrix[9] + matrix[13];
        out[2] = in[0] * matrix[2] + in[1] * matrix[6] + in[2] * matrix[10] + matrix[14];
        out[3] = in[3];
        out[4] = in[4];
        for (int k = 0; k < 3; k++) {
            if (out[k] < placed.world_bounds[k]) placed.world_bounds[k] = out[k];
            if (out[k] > placed.world_bounds[k+3]) placed.world_bounds[k+3] = out[k];
        }
        if (!isfinite(out[0]) || !isfinite(out[1]) || !isfinite(out[2]) ||
            fabsf(out[0]) > WINST_WORLD_LIMIT || fabsf(out[1]) > WINST_WORLD_LIMIT ||
            fabsf(out[2]) > WINST_WORLD_LIMIT) {
            winst_place_free(placed.verts);
            winst_place_free(placed.idx);
            winst_place_free(placed.vcol);
            winst_reject(stats);
            return 0;
        }
    }
    placed.world_bounds_valid = 1;
    placed.inst = 1;
    snprintf(placed.aname, sizeof placed.aname, "%.27s", asset_name ? asset_name : "");
    if (!winst_push_mesh(dst, placed)) {
        winst_place_free(placed.verts);
        winst_place_free(placed.idx);
        winst_place_free(placed.vcol);
        winst_reject(stats);
        return 0;
    }
    if (stats) stats->meshes_placed++;
    return 1;
}

static unsigned char winst_fold_char(unsigned char c) {
    return c >= 'a' && c <= 'z' ? (unsigned char)(c - ('a' - 'A')) : c;
}

static uint32_t winst_name_key(const char *name, char folded[28]) {
    uint32_t h = 2166136261u;
    int i = 0;
    if (name) {
        while (i < 27 && name[i]) {
            unsigned char c = winst_fold_char((unsigned char)name[i]);
            folded[i++] = (char)c;
            h = (h ^ c) * 16777619u;
        }
    }
    folded[i] = 0;
    return h;
}

static void winst_free_scene(N2Scene *scene) {
    if (!scene) return;
    for (int i = 0; i < scene->count; i++) {
        n2_mesh_geometry_release(&scene->meshes[i]);
    }
    free(scene->meshes);
    memset(scene, 0, sizeof *scene);
}

static void winst_library_free(WInstLibrary *library) {
    if (!library) return;
    free(library->model_index);
    for (int i = 0; i < library->count; i++) winst_free_scene(&library->items[i].scene);
    free(library->items);
    memset(library, 0, sizeof *library);
}

static int winst_library_add(WInstLibrary *library, N2Scene *scene,
                             const char *name, const float matrix[16], int has_matrix,
                             int is_vista) {
    if (library->count == library->cap) {
        int next = library->cap ? library->cap * 2 : 64;
        if (next <= library->cap || (size_t)next > SIZE_MAX / sizeof *library->items)
            return 0;
        WInstProto *grown = (WInstProto *)realloc(
            library->items, (size_t)next * sizeof *library->items);
        if (!grown) return 0;
        library->items = grown;
        library->cap = next;
    }
    WInstProto *proto = &library->items[library->count++];
    memset(proto, 0, sizeof *proto);
    proto->scene = *scene;
    proto->name_hash = winst_name_key(name, proto->name);
    memcpy(proto->matrix, matrix, sizeof proto->matrix);
    proto->has_matrix = (unsigned char)(has_matrix != 0);
    proto->is_vista = (unsigned char)(is_vista != 0);
    memset(scene, 0, sizeof *scene);
    return 1;
}

/* Hashes narrow candidate lookup only: the folded name comparison is mandatory
 * so two names with an equal 32-bit hash can never share a prototype. */
static const WInstProto *winst_library_find(const WInstLibrary *library,
                                            const char *asset_name,
                                            WInstStats *stats) {
    char folded[28];
    uint32_t key = winst_name_key(asset_name, folded);
    if (library) for (int i = 0; i < library->count; i++) {
        const WInstProto *proto = &library->items[i];
        if (proto->name_hash == key && !strcmp(proto->name, folded)) return proto;
    }
    if (stats) stats->missing_models++;
    return NULL;
}

/* U2 type records name up to three model keys (0x34102 +0x20/+0x24/+0x28).
 * Prefer the first available authored model; later keys are explicit LOD
 * alternatives, not names synthesized from A/B/Z suffixes. This is availability
 * fallback, not retail's still-undecoded distance/region LOD selection rule.
 * Name lookup is reserved for records without ANY authored key. */
/* Preserve the resolver's first-model semantics, including duplicate keys.
 * A neighborhood can resolve thousands of placements with three LOD keys;
 * scanning the entire city's library per key was repeated CPU work. */
static void winst_library_index(WInstLibrary *library) {
    if (!library || library->model_index || library->count <= 0) return;
    unsigned cap = 16;
    while (cap < (unsigned)library->count * 2u) {
        if (cap > UINT_MAX / 2u) return;
        cap *= 2u;
    }
    int *slots = (int *)calloc(cap, sizeof *slots);
    if (!slots) return; /* retain the existing linear fallback on allocation failure */
    for (int i = 0; i < library->count; i++) {
        uint32_t key = library->items[i].model_key;
        if (!key) continue;
        unsigned slot = (key * 2654435761u) & (cap - 1u);
        while (slots[slot] && library->items[slots[slot]-1].model_key != key)
            slot = (slot + 1u) & (cap - 1u);
        if (!slots[slot]) slots[slot] = i + 1;
    }
    library->model_index = slots;
    library->model_index_cap = cap;
}

static const WInstProto *winst_library_key(const WInstLibrary *library, uint32_t key) {
    if (!library || !key) return NULL;
    if (library->model_index) {
        unsigned mask = library->model_index_cap - 1u;
        unsigned slot = (key * 2654435761u) & mask;
        while (library->model_index[slot]) {
            const WInstProto *p = &library->items[library->model_index[slot]-1];
            if (p->model_key == key) return p;
            slot = (slot + 1u) & mask;
        }
        return NULL;
    }
    for (int i = 0; i < library->count; i++)
        if (library->items[i].model_key == key) return &library->items[i];
    return NULL;
}

static const WInstProto *winst_library_resolve(const WInstLibrary *library,
                                              const WInstPlacement *placement,
                                              const char *name,
                                              WInstStats *stats) {
    int keyed = 0;
    for (int lod = 0; lod < 3; lod++) {
        uint32_t key = placement->model_keys[lod];
        if (!key) continue;
        keyed = 1;
        const WInstProto *proto = winst_library_key(library, key);
        if (proto) {
            if (stats) {
                stats->keyed_models++;
                stats->lod_fallbacks += lod != 0;
            }
            return proto;
        }
    }
    if (keyed) {
        if (stats) stats->missing_models++;
        return NULL;
    }
    const WInstProto *proto = winst_library_find(library, name, stats);
    if (proto && stats) stats->unkeyed_models++;
    return proto;
}

/* An object's authored matrix belongs to the prototype, never a final mesh.
 * It is only a later fallback candidate when this is the unique ROAD/TERRAIN
 * prototype for a requested asset. */
static int winst_proto_has_own_matrix(const WInstLibrary *library,
                                      const WInstProto *proto) {
    if (!library || !proto || !proto->has_matrix || proto->mesh_count <= 0)
        return 0;
    if (!proto->all_ground) return 0;
    for (int i = 0; i < library->count; i++)
        if (&library->items[i] != proto &&
            !strcmp(library->items[i].name, proto->name)) return 0;
    return 1;
}

/* The U2 object header's stored name starts at +0xa4 after filler. Searching
 * binary matrix/bounds bytes for an ASCII-looking run can manufacture a name
 * before reaching it (e.g. the recovered tree models). Keep the legacy reader
 * for short/unknown layouts; model identity never depends on this display name. */
static uint32_t winst_model_identity(const unsigned char *data, long begin,
                                     long end, char *name, int cap) {
    n2_mesh_name(data, begin, end, name, cap);
    N2Leaf info[1]; int ninfo = 0;
    n2_find_leaves(data, begin, end, 0x00134011u, info, &ninfo, 1);
    if (!ninfo) return 0;
    const unsigned char *header = data + info[0].off;
    long pad = n2_skip_filler(header, (int)info[0].size);
    long length = info[0].size - pad;
    header += pad;
    uint32_t key = length >= 0x14 ? n2_u32(header + 0x10) : 0;
    if (cap <= 0 || length <= 0xa4) return key;
    long n = 0;
    while (0xa4 + n < length) {
        unsigned char c = header[0xa4 + n];
        if (!c) break;
        if (!(c == '_' || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return key;
        n++;
    }
    if (!n || 0xa4 + n == length) return key;
    if (n >= cap) n = cap - 1;
    memcpy(name, header + 0xa4, (size_t)n);
    name[n] = 0;
    return key;
}

/* reload == NULL: library scan; record the prototype's metadata and discard
 * its geometry. reload != NULL: re-parse that prototype's geometry into it. */
static void winst_collect_model(WInstLibrary *library, const unsigned char *data,
                                long begin, long end,
                                const uint32_t *keys, int nkeys,
                                WInstProto *reload) {
    N2Scene local;
    memset(&local, 0, sizeof local);
    int cat = n2_mesh_category(data, begin, end);
    char name[40];
    uint32_t model_key = winst_model_identity(data, begin, end, name, sizeof name);
    int scen = n2_scen_class(name);
    float matrix[16];
    int has_matrix = n2_obj_matrix(data, begin, end, matrix);
    int is_vista = 0;
    uint32_t slot[64];
    int nslot = n2_mesh_texslots(data, begin, end, slot, 64);
    /* Match n2_walk_meshes' established PAN_* contract. These authored
       horizon models render through the vista tier, but must never enter the
       ordinary scene where TERRAIN members become ground/collision support.
       The same two exceptions apply here, or free roam and a race would
       disagree about which objects are backdrop: PAN_OCEAN is the water
       surface, not a billboard, and three unnamed leftovers are backdrop that
       no name rule can see -- both are decided by measurement/material. */
    N2Geom geometry;
    if (!strncmp(name, "PAN", 3)) {
        is_vista = !(n2_obj_geom(data, begin, end, matrix, &geometry) &&
                     n2_is_ground_sheet(&geometry));
    } else if (n2_impostor_atlas(slot, nslot)) {
        is_vista = 1;
    } else if (n2_vista_family(name)) {
        is_vista = n2_obj_geom(data, begin, end, matrix, &geometry) &&
                   n2_is_vista_impostor(name, &geometry);
    }
    uint32_t texkey = n2_mesh_texkey_cat(data, begin, end, cat, keys, nkeys);
    N2Leaf vtx[64], idx[64];
    int nv = 0, ni = 0;
    n2_find_leaves(data, begin, end, 0x00134B01u, vtx, &nv, 64);
    n2_find_leaves(data, begin, end, 0x00134B03u, idx, &ni, 64);
    int pairs = nv < ni ? nv : ni;
    /* XS_ props carry 36-byte vertices: see n2_solid_vstride */
    const int vs = n2_solid_vstride(data, begin, end);
    const int vuv = vs == 36 ? 28 : 16, vcolo = vs == 36 ? 24 : 12;

    /* This deliberately mirrors n2_walk_meshes' verified world-material
     * partition gate. A malformed partition falls through to the
     * identical whole-object path instead of silently losing geometry. */
    int sub_ok = 0;
    N2Sub sub[64];
    int nsub = 0;
    if (cat != N2_GLOW && pairs == 1) {
        nsub = n2_mesh_submeshes(data, begin, end, sub, 64);
        if (nsub > 0 && nslot > 0) {
            const unsigned char *ib0 = data + idx[0].off;
            int ibytes = (int)idx[0].size, ip = 0;
            while (ip + 2 <= ibytes && ib0[ip] == 0x11 && ib0[ip + 1] == 0x11)
                ip += 2;
            long available = (ibytes - ip) / 2;
            long chain = 0;
            sub_ok = 1;
            for (int i = 0; i < nsub && sub_ok; i++) {
                if (sub[i].mat >= (uint32_t)nslot || !slot[sub[i].mat] ||
                    (long)sub[i].start != chain || sub[i].count < 3 ||
                    chain + (long)sub[i].count > available)
                    sub_ok = 0;
                else chain += (long)sub[i].count;
            }
            if (sub_ok && chain != available - available % 3) sub_ok = 0;
        }
    }
    {   /* Diagnostic twin of nfsu2.h's OPENUG2_OBJ_PROBE, for the instance
           walker: which texture slot does each submesh of this object
           select, and does this bundle's key set actually supply it? */
        static const char *probe = (const char *)1;
        if (probe == (const char *)1) probe = getenv("OPENUG2_SLOT_PROBE");
        if (probe && name[0] && n2_icontains((const unsigned char *)name, (long)strlen(name), probe)) {
            fprintf(stderr, "SLOTPROBE %-28s nslot=%d nsub=%d sub_ok=%d pairs=%d cat=%d texkey=%08x\n",
                    name, nslot, nsub, sub_ok, pairs, cat, texkey);
            for (int q = 0; q < nslot; q++)
                fprintf(stderr, "SLOTPROBE   slot[%d]=%08x resolved=%08x\n",
                        q, slot[q], n2_resolve_key(slot[q], keys, nkeys));
            for (int q = 0; q < nsub; q++)
                fprintf(stderr, "SLOTPROBE   sub[%d] start=%u count=%u mat=%u matid=%08x\n",
                        q, sub[q].start, sub[q].count, sub[q].mat, sub[q].matid);
        }
    }
    if (sub_ok) {
        for (int i = 0; i < nsub; i++) {
            uint32_t authored = slot[sub[i].mat];
            /* A light-pool card has no diffuse map anywhere to fall back to;
               emitting it means painting a sibling texture across it. */
            if (n2_slot_is_lightpool(authored)) continue;
            /* SKYDOME resolves from shared LOC4 after prototype placement,
               not from this bundle's regional key inventory. Preserve the
               verified positional slot exactly, matching n2_walk_meshes. */
            uint32_t subkey = cat == N2_SKY
                            ? authored : n2_resolve_key(authored, keys, nkeys);
            int exact = subkey != 0;
            int before = local.count;
            n2_add_pair(data, vtx[0], idx[0], cat, &local, vs, vuv, vcolo, cat != N2_SKY,
                        subkey ? subkey : texkey, NULL,
                        (long)sub[i].start, (long)sub[i].count,
                        (unsigned char)N2_DRAW_OPAQUE);
            for (int j = before; j < local.count; j++) {
                local.meshes[j].mat_exact = (unsigned char)exact;
                local.meshes[j].reflect = (sub[i].flags & N2_SUB_REFLECT) != 0;
            }
        }
    } else {
        int exact_single_slot = nslot == 1 && slot[0] != 0;
        for (int i = 0; i < pairs; i++) {
            int before = local.count;
            n2_add_pair(data, vtx[i], idx[i], cat, &local, vs, vuv, vcolo, cat != N2_SKY,
                        texkey, NULL, 0, -1, (unsigned char)N2_DRAW_OPAQUE);
            for (int j = before; j < local.count; j++)
                local.meshes[j].mat_exact = (unsigned char)exact_single_slot;
        }
    }
    for (int i = 0; i < local.count; i++) {
        local.meshes[i].scen = (unsigned char)scen;
        snprintf(local.meshes[i].sname, sizeof local.meshes[i].sname, "%.31s", name);
    }
    if (reload) {
        reload->scene = local;
        return;
    }
    int count = local.count, any_ground = 0, all_ground = 1, index_count = 0;
    for (int i = 0; i < count; i++) {
        int g = local.meshes[i].cat == N2_ROAD || local.meshes[i].cat == N2_TERRAIN;
        any_ground |= g;
        all_ground &= g;
        index_count += local.meshes[i].nidx;
    }
    winst_free_scene(&local);
    N2Scene empty;
    memset(&empty, 0, sizeof empty);
    if (count && winst_library_add(library, &empty, name, matrix,
                                   has_matrix, is_vista)) {
        WInstProto *proto = &library->items[library->count - 1];
        proto->model_key = model_key;
        proto->mesh_count = count;
        proto->index_count = index_count;
        proto->any_ground = (unsigned char)any_ground;
        proto->all_ground = (unsigned char)all_ground;
        proto->begin = begin;
        proto->end = end;
    }
}

/* Parse a prototype's geometry on first use. */
static const N2Scene *winst_proto_load(const WInstLibrary *library, WInstProto *proto) {
    if (!proto->loaded) {
        winst_collect_model((WInstLibrary *)library, library->data, proto->begin, proto->end,
                            library->keys, library->nkeys, proto);
        proto->loaded = 1;
    }
    return &proto->scene;
}

static void winst_proto_unload(WInstProto *proto) {
    winst_free_scene(&proto->scene);
    proto->loaded = 0;
}

static void winst_collect_models(WInstLibrary *library, const unsigned char *data,
                                 long begin, long end,
                                 const uint32_t *keys, int nkeys) {
    if (!library || !data || begin < 0 || end < begin) return;
    for (long offset = begin; offset + 8 <= end;) {
        uint32_t magic = n2_u32(data + offset);
        uint32_t size = n2_u32(data + offset + 4);
        long payload = offset + 8;
        if ((uint64_t)size > (uint64_t)(end - payload)) return;
        long object_end = payload + (long)size;
        if (magic == 0x80134010u)
            winst_collect_model(library, data, payload, object_end, keys, nkeys, NULL);
        else if (magic != 0 && (magic >> 28) == 8)
            winst_collect_models(library, data, payload, object_end, keys, nkeys);
        offset = object_end;
    }
}

static int chunk_end(long off, uint32_t size, long limit, long *end) {
    if (off < 0 || off > limit || (long)size < 0) return 0;
    if ((uint64_t)size > (uint64_t)(limit - off)) return 0;
    *end = off + (long)size;
    return 1;
}

static uint16_t winst_u16(const unsigned char *p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static int16_t winst_s16(const unsigned char *p) {
    return (int16_t)winst_u16(p);
}

static float winst_f32(const unsigned char *p) {
    float v;
    memcpy(&v, p, sizeof v);
    return v;
}

static long skip_filler(const unsigned char *p, long len) {
    long n = 0;
    while (n < len && p[n] == 0x11) n++;
    return n;
}

static void free_region_array(WInstRegion *regions, int count) {
    if (!regions) return;
    for (int i = 0; i < count; i++) free(regions[i].xy);
    free(regions);
}

static int append_region(WInstRegion **regions, int *count, int *cap,
                         const unsigned char *record, int nxy) {
    if (*count == *cap) {
        int next = *cap ? *cap * 2 : 16;
        if (next <= *cap || (size_t)next > SIZE_MAX / sizeof **regions) return 0;
        WInstRegion *grown = (WInstRegion *)realloc(
            *regions, (size_t)next * sizeof **regions);
        if (!grown) return 0;
        *regions = grown;
        *cap = next;
    }

    WInstRegion *r = &(*regions)[*count];
    r->id = (int)winst_u16(record + 8);
    for (int i = 0; i < 4; i++) r->bb[i] = winst_f32(record + 0x0c + i * 4);
    r->nxy = nxy;
    r->xy = (float *)malloc((size_t)nxy * 2 * sizeof *r->xy);
    if (!r->xy) return 0;
    for (int i = 0; i < nxy * 2; i++) r->xy[i] = winst_f32(record + 0x24 + i * 4);
    (*count)++;
    return 1;
}

static int parse_polygon_payload(const unsigned char *data, long len,
                                 WInstRegion **regions, int *count, int *cap) {
    long pos = skip_filler(data, len);
    while (pos < len) {
        long remain = len - pos;
        if (remain < 0x24) return 0;
        const unsigned char *record = data + pos;
        int nxy = (int)winst_u16(record + 0x0a);
        if (nxy < 1 || nxy > 64) return 0;
        long record_size = 0x24 + (long)nxy * 8;
        if (record_size > remain) return 0;
        if (!append_region(regions, count, cap, record, nxy)) return 0;
        pos += record_size;
        pos += skip_filler(data + pos, len - pos);
    }
    return 1;
}

int winst_parse_regions(const unsigned char *data, long len,
                        WInstRegion **out, int *count) {
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!data || len < 0 || !out || !count) return 0;

    WInstRegion *regions = NULL;
    int nregions = 0, cap = 0;
    long pos = 0;
    while (pos < len) {
        if (len - pos < 8) {
            free_region_array(regions, nregions);
            return 0;
        }
        uint32_t magic = n2_u32(data + pos);
        long payload_end = 0;
        if (!chunk_end(pos + 8, n2_u32(data + pos + 4), len, &payload_end)) {
            free_region_array(regions, nregions);
            return 0;
        }
        if (magic == 0x80034150u) {
            long child = pos + 8;
            while (child < payload_end) {
                if (payload_end - child < 8) {
                    free_region_array(regions, nregions);
                    return 0;
                }
                uint32_t child_magic = n2_u32(data + child);
                long child_end = 0;
                if (!chunk_end(child + 8, n2_u32(data + child + 4),
                               payload_end, &child_end)) {
                    free_region_array(regions, nregions);
                    return 0;
                }
                if (child_magic == 0x00034152u &&
                    !parse_polygon_payload(data + child + 8,
                                           child_end - (child + 8),
                                           &regions, &nregions, &cap)) {
                    free_region_array(regions, nregions);
                    return 0;
                }
                child = child_end;
            }
        }
        pos = payload_end;
    }

    if (nregions == 0) {
        free(regions);
        return 0;
    }
    *out = regions;
    *count = nregions;
    return nregions;
}

void winst_free_regions(WInstRegion *regions, int count) {
    if (count < 0) count = 0;
    free_region_array(regions, count);
}

static int point_on_segment(float x, float y,
                            float x0, float y0, float x1, float y1) {
    float cross = (x - x0) * (y1 - y0) - (y - y0) * (x1 - x0);
    float scale = fabsf(x1 - x0) + fabsf(y1 - y0) + 1.0f;
    if (fabsf(cross) > 1e-6f * scale) return 0;
    return x >= fminf(x0, x1) - 1e-6f && x <= fmaxf(x0, x1) + 1e-6f &&
           y >= fminf(y0, y1) - 1e-6f && y <= fmaxf(y0, y1) + 1e-6f;
}

static int in_polygon(const WInstRegion *r, float x, float y) {
    if (x < r->bb[0] || x > r->bb[2] || y < r->bb[1] || y > r->bb[3]) return 0;
    int inside = 0;
    for (int i = 0, j = r->nxy - 1; i < r->nxy; j = i++) {
        float xi = r->xy[2 * i], yi = r->xy[2 * i + 1];
        float xj = r->xy[2 * j], yj = r->xy[2 * j + 1];
        if (point_on_segment(x, y, xi, yi, xj, yj)) return 1;
        if ((yi > y) != (yj > y)) {
            float crossing = (xj - xi) * (y - yi) / (yj - yi) + xi;
            if (x < crossing) inside = !inside;
        }
    }
    return inside;
}

int winst_select_regions(const WInstRegion *regions, int count,
                         float x, float y, float radius,
                         unsigned char *selected, int selected_cap,
                         int *home_index) {
    if (home_index) *home_index = -1;
    if (!regions || count < 0 || !selected || selected_cap < 0 || radius < 0.0f)
        return 0;
    for (int i = 0; i < count; i++)
        if (regions[i].id < 0 || regions[i].id >= selected_cap) return 0;

    memset(selected, 0, (size_t)selected_cap);
    float radius2 = radius * radius;
    int nselected = 0;
    double home_area = 0.0;
    for (int i = 0; i < count; i++) {
        const WInstRegion *r = &regions[i];
        if (in_polygon(r, x, y) && home_index) {
            double twice_area = 0.0;
            for (int p = 0, q = r->nxy - 1; p < r->nxy; q = p++)
                twice_area += (double)r->xy[2 * q] * r->xy[2 * p + 1] -
                              (double)r->xy[2 * p] * r->xy[2 * q + 1];
            double area = fabs(twice_area);
            if (*home_index < 0 || area < home_area) {
                *home_index = i;
                home_area = area;
            }
        }

        float dx = x < r->bb[0] ? r->bb[0] - x : x > r->bb[2] ? x - r->bb[2] : 0.0f;
        float dy = y < r->bb[1] ? r->bb[1] - y : y > r->bb[3] ? y - r->bb[3] : 0.0f;
        if (dx * dx + dy * dy <= radius2) {
            selected[r->id] = 1;
            nselected++;
        }
    }
    return nselected;
}

int winst_decode_placement(const unsigned char *record, long len,
                           WInstPlacement *out) {
    if (!record || !out || len < 64) return 0;
    memset(out, 0, sizeof *out);
    out->type_index = winst_u16(record + 0x18);
    out->flags = winst_u16(record + 0x1a);
    for (int i = 0; i < 3; i++) {
        out->bounds_min[i] = winst_f32(record + i * 4);
        out->bounds_max[i] = winst_f32(record + 0x0c + i * 4);
    }
    for (int i = 0; i < 16; i++) out->matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++)
            out->matrix[row * 4 + col] =
                (float)winst_s16(record + 0x2c + (row * 3 + col) * 2) / 8192.0f;
    out->matrix[12] = winst_f32(record + 0x20);
    out->matrix[13] = winst_f32(record + 0x24);
    out->matrix[14] = winst_f32(record + 0x28);
    return 1;
}

/* A record whose transform is the identity is the authored world-space ground
 * chunk contract: the prototype's vertices are already map coordinates, so the
 * ground pass emits it once and the section walk leaves it alone. Exact
 * comparison is sound -- the 3x3 arrives as s16/8192, where 1.0 and 0.0 are
 * exact, and the translation is read straight out of the file. */
static int winst_identity_placement(const float m[16]) {
    return m[12] == 0.0f && m[13] == 0.0f && m[14] == 0.0f &&
           m[0] == 1.0f && m[5] == 1.0f && m[10] == 1.0f &&
           m[1] == 0.0f && m[2] == 0.0f && m[4] == 0.0f &&
           m[6] == 0.0f && m[8] == 0.0f && m[9] == 0.0f;
}

typedef struct {
    int region_id;
    const unsigned char *types;
    int type_count;
    const unsigned char *placements;
    int placement_count;
} WInstSection;

typedef int (*WInstInternalVisitFn)(const WInstPlacement *placement,
                                    const char *type_name, void *userdata);

static int winst_fixed_records(const unsigned char *data, long len, int stride,
                               const unsigned char **records, int *count) {
    if (!data || len < 0 || stride <= 0 || !records || !count) return 0;
    long filler = len % stride == 0 ? 0 : skip_filler(data, len);
    long body = len - filler;
    if (body < 0 || body % stride || body / stride > INT_MAX) return 0;
    *records = data + filler;
    *count = (int)(body / stride);
    return 1;
}

static int winst_parse_section(const unsigned char *data, long begin, long end,
                               WInstSection *section) {
    if (!data || !section || begin < 0 || end < begin) return 0;
    memset(section, 0, sizeof *section);
    section->region_id = -1;
    const unsigned char *info = NULL, *types = NULL, *placements = NULL;
    long info_len = 0, type_len = 0, placement_len = 0;
    for (long pos = begin; pos < end;) {
        if (end - pos < 8) return 0;
        uint32_t magic = n2_u32(data + pos);
        long child_end = 0;
        if (!chunk_end(pos + 8, n2_u32(data + pos + 4), end, &child_end)) return 0;
        if (magic == 0x00034101u) { info = data + pos + 8; info_len = child_end - pos - 8; }
        else if (magic == 0x00034102u) { types = data + pos + 8; type_len = child_end - pos - 8; }
        else if (magic == 0x00034103u) {
            placements = data + pos + 8;
            placement_len = child_end - pos - 8;
        }
        pos = child_end;
    }
    if (!info || info_len < 0x10 || !types || !placements) return 0;
    uint32_t region_id = n2_u32(info + 0x0c);
    if (region_id > INT_MAX) return 0;
    section->region_id = (int)region_id;
    if (!winst_fixed_records(types, type_len, 68,
                             &section->types, &section->type_count) ||
        !winst_fixed_records(placements, placement_len, 64,
                             &section->placements, &section->placement_count))
        return 0;
    return section->type_count > 0;
}

static int winst_bbox_in_range(const WInstPlacement *placement,
                               float x, float y, float radius) {
    if (!placement || !isfinite(x) || !isfinite(y) || !isfinite(radius) ||
        radius < 0.0f) return 0;
    for (int i = 0; i < 3; i++)
        if (!isfinite(placement->bounds_min[i]) ||
            !isfinite(placement->bounds_max[i]) ||
            placement->bounds_min[i] > placement->bounds_max[i]) return 0;
    float dx = x < placement->bounds_min[0] ? placement->bounds_min[0] - x
             : x > placement->bounds_max[0] ? x - placement->bounds_max[0] : 0.0f;
    float dy = y < placement->bounds_min[1] ? placement->bounds_min[1] - y
             : y > placement->bounds_max[1] ? y - placement->bounds_max[1] : 0.0f;
    return dx * dx + dy * dy <= radius * radius;
}

#ifdef __vita__
static Uint64 g_visit_ticks;   /* time spent placing (vs scanning) this build */
#endif
static int winst_visit_section(const WInstSection *section,
                               float focus_x, float focus_y, float view_radius,
                               WInstInternalVisitFn visit, void *userdata,
                               WInstStats *stats, const WGSelection *scenery) {
    for (int i = 0; i < section->placement_count; i++) {
        WInstPlacement placement;
        const unsigned char *record = section->placements + (long)i * 64;
        if (stats) stats->instances_seen++;
        if (!winst_decode_placement(record, 64, &placement)) {
            winst_reject(stats);
            continue;
        }
        placement.record_id = record;
        placement.region_id = section->region_id;
        placement.record_index = i;
        if (!winst_bbox_in_range(&placement, focus_x, focus_y, view_radius)) continue;
        if (stats) stats->instances_in_range++;
        if (placement.type_index >= (unsigned)section->type_count) {
            winst_reject(stats);
            continue;
        }
        if (!wg_selection_visible(scenery, (unsigned)section->region_id, (unsigned)i)) {
            if (stats) stats->scenery_hidden++;
            continue;
        }
        char type_name[33];
        const unsigned char *type = section->types + (long)placement.type_index * 68;
        memcpy(type_name, type, 32);
        type_name[32] = 0;
        for (int lod = 0; lod < 3; lod++)
            placement.model_keys[lod] = n2_u32(type + 0x20 + lod * 4);
#ifdef __vita__
        Uint64 t0 = SDL_GetPerformanceCounter();
        int ok = !visit || visit(&placement, type_name, userdata);
        g_visit_ticks += SDL_GetPerformanceCounter() - t0;
        if (!ok) return 0;
#else
        if (visit && !visit(&placement, type_name, userdata)) return 0;
#endif
    }
    return 1;
}

typedef struct {
    const unsigned char *selected;
    int selected_cap;
    int find_region;
    int found_region;
    float focus_x, focus_y, view_radius;
    WInstInternalVisitFn visit;
    void *userdata;
    WInstStats *stats;
    const WGSelection *scenery;
} WInstWalk;

static int winst_walk_sections(const unsigned char *data, long begin, long end,
                               WInstWalk *walk) {
    for (long pos = begin; pos < end;) {
        if (end - pos < 8) return 0;
        uint32_t magic = n2_u32(data + pos);
        long child_end = 0;
        if (!chunk_end(pos + 8, n2_u32(data + pos + 4), end, &child_end)) return 0;
        if (magic == 0x80034100u) {
            WInstSection section;
            if (!winst_parse_section(data, pos + 8, child_end, &section)) return 0;
            if (section.region_id == walk->find_region) walk->found_region = 1;
            int selected = !walk->selected ||
                (section.region_id >= 0 && section.region_id < walk->selected_cap &&
                 walk->selected[section.region_id]);
            if (selected && walk->visit &&
                !winst_visit_section(&section, walk->focus_x, walk->focus_y,
                                     walk->view_radius, walk->visit,
                                     walk->userdata, walk->stats, walk->scenery)) return 0;
        } else if (magic != 0 && (magic >> 28) == 8) {
            if (!winst_walk_sections(data, pos + 8, child_end, walk)) return 0;
        }
        pos = child_end;
    }
    return 1;
}

static int winst_mark_section_ids(const unsigned char *data, long begin, long end,
                                  unsigned depth, unsigned char *present,
                                  int present_cap) {
    if (depth > 64) return 0;
    for (long pos=begin; pos<end;) {
        if (end-pos<8) return 0;
        uint32_t magic=n2_u32(data+pos); long child_end=0;
        if (!chunk_end(pos+8,n2_u32(data+pos+4),end,&child_end)) return 0;
        if (magic==0x80034100u) {
            WInstSection section;
            if (!winst_parse_section(data,pos+8,child_end,&section) ||
                section.region_id<0 || section.region_id>=present_cap) return 0;
            present[section.region_id]=1;
        } else if (magic && (magic>>28)==8 &&
                   !winst_mark_section_ids(data,pos+8,child_end,depth+1,
                                           present,present_cap)) return 0;
        pos=child_end;
    }
    return 1;
}

static unsigned char *winst_read_named(const char *root, const char *name, long *len);
int world_instance_district_boxes(const char *track_root, const char *bundle,
                                  float (**boxes)[4], int *count) {
    if (!track_root || !bundle || !boxes || !count) return 0;
    *boxes = NULL; *count = 0;
    const char *stem = !strncmp(bundle, "STREAM", 6) ? bundle + 6 : bundle;
    char companion[64];
    snprintf(companion, sizeof companion, "%s", stem);
    char *dot = strrchr(companion, '.');
    if (dot && !strcmp(dot, ".BUN")) *dot = 0;
    long len = 0;
    unsigned char *data = winst_read_named(track_root, companion, &len);
    if (!data) return 0;
    WInstRegion *regions = NULL; int n = 0;
    int ok = winst_parse_regions(data, len, &regions, &n) && n > 0;
    free(data);
    if (ok && (*boxes = (float (*)[4])malloc((size_t)n * sizeof **boxes)) != NULL) {
        for (int i = 0; i < n; i++) memcpy((*boxes)[i], regions[i].bb, sizeof (*boxes)[i]);
        *count = n;
    } else ok = 0;
    winst_free_regions(regions, n);
    return ok;
}

int world_instance_bounds(const char *track_root, const char *bundle, float out[4]) {
    if (!track_root || !bundle || !out) return 0;
    const char *stem = !strncmp(bundle, "STREAM", 6) ? bundle + 6 : bundle;
    char companion[64];
    snprintf(companion, sizeof companion, "%s", stem);
    char *dot = strrchr(companion, '.');
    if (dot && !strcmp(dot, ".BUN")) *dot = 0;
    long len = 0;
    unsigned char *data = winst_read_named(track_root, companion, &len);
    if (!data) return 0;
    WInstRegion *regions = NULL; int count = 0;
    int ok = winst_parse_regions(data, len, &regions, &count) && count > 0;
    free(data);
    if (!ok) { winst_free_regions(regions, count); return 0; }
    out[0] = out[1] = 1e30f; out[2] = out[3] = -1e30f;
    for (int i = 0; i < count; i++) {
        if (regions[i].bb[0] < out[0]) out[0] = regions[i].bb[0];
        if (regions[i].bb[1] < out[1]) out[1] = regions[i].bb[1];
        if (regions[i].bb[2] > out[2]) out[2] = regions[i].bb[2];
        if (regions[i].bb[3] > out[3]) out[3] = regions[i].bb[3];
    }
    winst_free_regions(regions, count);
    return 1;
}

int winst_default_focus(const unsigned char *companion, long companion_len,
                        const unsigned char *stream, long stream_len,
                        float out_xy[2]) {
    if (!companion || companion_len < 0 || !stream || stream_len < 0 || !out_xy)
        return 0;
    WInstRegion *regions = NULL;
    int count = 0;
    if (!winst_parse_regions(companion, companion_len, &regions, &count)) return 0;
    unsigned char *present=(unsigned char *)calloc(65536,1);
    if (!present || !winst_mark_section_ids(stream,0,stream_len,0,present,65536)) {
        free(present);
        winst_free_regions(regions,count);
        return 0;
    }

    int best = -1;
    float best_x = 0.0f, best_y = 0.0f;
    double best_d2 = 0.0;
    for (int i = 0; i < count; i++) {
        if (regions[i].id<0 || regions[i].id>=65536 || !present[regions[i].id]) continue;

        double sx = 0.0, sy = 0.0;
        for (int p = 0; p < regions[i].nxy; p++) {
            sx += regions[i].xy[p * 2];
            sy += regions[i].xy[p * 2 + 1];
        }
        float x = (float)(sx / regions[i].nxy);
        float y = (float)(sy / regions[i].nxy);
        if (!in_polygon(&regions[i], x, y)) {
            x = 0.5f * (regions[i].bb[0] + regions[i].bb[2]);
            y = 0.5f * (regions[i].bb[1] + regions[i].bb[3]);
        }
        if (!in_polygon(&regions[i], x, y)) {
            x = regions[i].xy[0];
            y = regions[i].xy[1];
        }
        double d2 = (double)x * x + (double)y * y;
        if (best < 0 || d2 < best_d2 ||
            (d2 == best_d2 && regions[i].id < regions[best].id)) {
            best = i;
            best_x = x;
            best_y = y;
            best_d2 = d2;
        }
    }
    free(present);
    winst_free_regions(regions, count);
    if (best < 0) return 0;
    out_xy[0] = best_x;
    out_xy[1] = best_y;
    return 1;
}

#ifdef WORLD_INSTANCE_TESTING
int winst_test_collect_placements(const unsigned char *section_data,
                                  long section_len,
                                  float focus_x, float focus_y,
                                  float view_radius,
                                  WInstVisitFn visit, void *userdata,
                                  WInstStats *stats) {
    if (!section_data || section_len < 0 || !visit) return 0;
    WInstWalk walk;
    memset(&walk, 0, sizeof walk);
    walk.find_region = -1;
    walk.focus_x = focus_x;
    walk.focus_y = focus_y;
    walk.view_radius = view_radius;
    walk.visit = visit;
    walk.userdata = userdata;
    walk.stats = stats;
    return winst_walk_sections(section_data, 0, section_len, &walk);
}
#endif

typedef struct {
    WInstLibrary *library;
    N2Scene *scene;
    N2Scene *vista;
    WInstStats *stats;
} WInstBuildVisit;

static int winst_build_visit_uncached(const WInstPlacement *placement,
                             const char *type_name, void *userdata) {
    WInstBuildVisit *build = (WInstBuildVisit *)userdata;
#ifdef __vita__
    if (world_build_aborted()) return 0;
#endif
    const WInstProto *proto = winst_library_resolve(build->library, placement,
                                                    type_name, build->stats);
    if (!proto) return 1;
    const N2Scene *meshes = winst_proto_load(build->library, (WInstProto *)proto);
    const WInstProto *low = proto;
    /* Only authored scenery alternatives. Ground, vistas and category mixtures
     * stay intact. Keep the original geometry for collision separately. */
    int compatible = !proto->any_ground && !proto->is_vista && meshes->count > 0;
    for (int i = 0; i < meshes->count; i++)
        compatible &= meshes->meshes[i].cat == N2_OTHER;
    if (g_world_low_detail && compatible) {
        for (int lod = 0; lod < 3; lod++) {
            uint32_t key = placement->model_keys[lod];
            if (!key) continue;
            const WInstProto *p = winst_library_key(build->library, key);
            if (p && !p->any_ground && !p->is_vista &&
                p->index_count > 0 && p->index_count < low->index_count) {
                const N2Scene *candidate = winst_proto_load(build->library, (WInstProto *)p);
                int ok = candidate->count > 0;
                for (int k = 0; k < candidate->count; k++)
                    ok &= candidate->meshes[k].cat == N2_OTHER &&
                          candidate->meshes[k].scen == meshes->meshes[0].scen;
                if (ok) low = p;
            }
        }
    }
    N2Scene *dst = proto->is_vista ? build->vista : build->scene;
    for (int i = 0; i < meshes->count; i++) {
        const N2Mesh *mesh = &meshes->meshes[i];
        int ground = mesh->cat == N2_ROAD || mesh->cat == N2_TERRAIN;
        /* A chunk prototype belongs to the ground pass outright. An instanced
         * one is placed here, at its own record's matrix, like anything else:
         * leaving it to the ground pass stranded every copy on one authored
         * origin, up to 855 m from where the record puts it. */
        if (ground && !proto->instanced) continue;
        N2Mesh source = *mesh;
        if (low != proto) {
            /* Collision copy; its prelighting is kept so a build that places
               it near the car can draw it instead of the stand-in. */
            source.world_role = 1;
        }
        if (!winst_place_mesh(dst, &source, placement->matrix, type_name,
                             build->stats)) return 0;
        if (ground && build->stats) build->stats->instanced_ground_meshes++;
    }
    if (low != proto) {
        const N2Scene *simple = &low->scene;
        for (int i = 0; i < simple->count; i++) {
            N2Mesh source = simple->meshes[i];
            source.world_role = 2;
            if (!winst_place_mesh(dst, &source, placement->matrix, type_name, build->stats)) return 0;
        }
        if (build->stats) {
            build->stats->lod_replacements++;
            build->stats->lod_saved_indices += proto->index_count - low->index_count;
        }
    }
    return 1;
}

#ifdef __vita__
/* Cache source placements inside fixed 128 m tiles. Each resident owns its
 * mesh metadata, while the tile and residents share immutable payloads. Only
 * the serial builder touches this table; final payload release may occur on
 * the render thread and therefore uses atomic reference counts. */
typedef struct WInstCachedPlacement {
    struct WInstCachedPlacement *next;
    const unsigned char *record;
    int tile_x, tile_y;
    N2Scene scene;
    size_t bytes;
    unsigned touched;
    int ground;
    long lod_replacements, lod_saved_indices;
} WInstCachedPlacement;
#define WINST_CACHE_SLOTS 4096
#define WINST_CACHE_LIMIT (24u * 1024u * 1024u)
#define WINST_GROUND_CACHE_LIMIT (8u * 1024u * 1024u)
#define WINST_SCENERY_CACHE_LIMIT (WINST_CACHE_LIMIT-WINST_GROUND_CACHE_LIMIT)
static WInstCachedPlacement *g_placement_cache[WINST_CACHE_SLOTS];
static size_t g_placement_cache_bytes;
static size_t g_ground_cache_bytes;
static const unsigned char *g_placement_cache_bundle;
static int g_placement_cache_detail, g_placement_cache_event;
static long g_cpu_cache_hits, g_cpu_cache_misses;
static long g_ground_cache_hits;
typedef struct { WInstCachedPlacement *entry; float distance2; unsigned slot; int ground; } WInstCacheVictim;
static WInstCacheVictim *g_cache_victims;
static int g_cache_victim_count, g_cache_victim_next[2];
static unsigned g_cache_epoch;
static float g_cache_focus_x, g_cache_focus_y;
static long g_cache_evicted;
static long g_lod_near_kept;   /* placements drawn full near the centre (this build) */
size_t world_instance_cache_bytes(void) { return g_placement_cache_bytes; }
/* Drop every cached placement (payloads still used by the active resident
   stay alive through their reference counts). Called after a build failed
   for memory: the next build is slower without reuse, but the heap gets back
   the cache's scattered blocks. Only while no build job runs. */
void world_instance_cache_flush(void) {
    long n = 0; size_t bytes = g_placement_cache_bytes;
    for (int i = 0; i < WINST_CACHE_SLOTS; i++)
        while (g_placement_cache[i]) {
            WInstCachedPlacement *e = g_placement_cache[i];
            g_placement_cache[i] = e->next;
            winst_free_scene(&e->scene); free(e); n++;
        }
    g_placement_cache_bytes = g_ground_cache_bytes = 0;
    free(g_cache_victims); g_cache_victims = NULL;
    g_cache_victim_count = g_cache_victim_next[0] = g_cache_victim_next[1] = 0;
    printf("placement cache flushed: %ld entries, %u KB\n", n, (unsigned)(bytes >> 10));
}

static float winst_cache_distance2(int tile_x, int tile_y) {
    float dx=(tile_x+0.5f)*128-g_cache_focus_x;
    float dy=(tile_y+0.5f)*128-g_cache_focus_y;
    return dx*dx+dy*dy;
}
static int winst_cache_victim_cmp(const void *a, const void *b) {
    float x=((const WInstCacheVictim *)a)->distance2;
    float y=((const WInstCacheVictim *)b)->distance2;
    return x>y ? -1 : x<y;
}

static size_t winst_cache_category_bytes(int ground) {
    return ground ? g_ground_cache_bytes : g_placement_cache_bytes-g_ground_cache_bytes;
}

static int winst_cache_make_room(size_t bytes, int tile_x, int tile_y, int ground) {
    size_t limit=ground ? WINST_GROUND_CACHE_LIMIT : WINST_SCENERY_CACHE_LIMIT;
    if (bytes > limit) return 0;
    float distance2=winst_cache_distance2(tile_x,tile_y);
    while (bytes > limit-winst_cache_category_bytes(ground) &&
           g_cache_victim_next[ground] < g_cache_victim_count) {
        WInstCacheVictim *v=&g_cache_victims[g_cache_victim_next[ground]];
        /* Sorted farthest first. Prefer a materially closer incoming tile;
           do not churn tiles already reused by this build. */
        if (v->distance2 <= distance2 + 128*128) break;
        g_cache_victim_next[ground]++;
        /* Other category may already have freed its entry. The snapshot's
           category is safe to inspect without touching that payload. */
        if (v->ground!=ground) continue;
        WInstCachedPlacement *e=v->entry;
        if (e->touched==g_cache_epoch) continue;
        WInstCachedPlacement **p=&g_placement_cache[v->slot];
        while (*p && *p!=e) p=&(*p)->next;
        assert(*p==e);
        *p=e->next; g_placement_cache_bytes-=e->bytes;
        if (e->ground) g_ground_cache_bytes-=e->bytes;
        winst_free_scene(&e->scene); free(e); g_cache_evicted++;
    }
    return bytes <= limit-winst_cache_category_bytes(ground);
}

static unsigned winst_cache_slot(const unsigned char *record) {
    return ((uintptr_t)record >> 6) & (WINST_CACHE_SLOTS - 1);
}

static void winst_cache_begin(const unsigned char *bundle, int event,
                             float x, float y, float radius) {
    free(g_cache_victims); g_cache_victims=NULL;
    g_cache_victim_count=g_cache_victim_next[0]=g_cache_victim_next[1]=0;
    g_cache_focus_x=x; g_cache_focus_y=y;
    g_cache_epoch++; g_cache_evicted=0;
    g_lod_near_kept=0;
    int entries=0;
    int reset = bundle != g_placement_cache_bundle ||
        event != g_placement_cache_event || g_world_low_detail != g_placement_cache_detail;
    for (int i = 0; i < WINST_CACHE_SLOTS; i++) {
        WInstCachedPlacement **p = &g_placement_cache[i];
        while (*p) {
            WInstCachedPlacement *e = *p;
            float dx = (e->tile_x + 0.5f) * 128 - x;
            float dy = (e->tile_y + 0.5f) * 128 - y;
            if (reset || dx*dx + dy*dy > (radius+256)*(radius+256)) {
                *p = e->next; g_placement_cache_bytes -= e->bytes;
                if (e->ground) g_ground_cache_bytes-=e->bytes;
                winst_free_scene(&e->scene); free(e);
                g_cache_evicted++;
            } else { entries++; p = &e->next; }
        }
    }
    g_placement_cache_bundle = bundle; g_placement_cache_event = event;
    g_placement_cache_detail = g_world_low_detail;
    g_cpu_cache_hits = g_cpu_cache_misses = 0;
    g_ground_cache_hits=0;
    if (entries) g_cache_victims=malloc((size_t)entries*sizeof *g_cache_victims);
    if (g_cache_victims) {
        for (unsigned i=0; i<WINST_CACHE_SLOTS; i++)
            for (WInstCachedPlacement *e=g_placement_cache[i]; e; e=e->next) {
                WInstCacheVictim *v=&g_cache_victims[g_cache_victim_count++];
                v->entry=e; v->slot=i;
                v->ground=e->ground;
                v->distance2=winst_cache_distance2(e->tile_x,e->tile_y);
            }
        qsort(g_cache_victims,(size_t)g_cache_victim_count,sizeof *g_cache_victims,
              winst_cache_victim_cmp);
    }
}
#endif


#ifdef __vita__
static int winst_cache_store(N2Scene *scene, int before, const unsigned char *record,
                             int tile_x, int tile_y, int ground,
                             long lod_replacements, long lod_saved_indices) {
    int count = scene->count - before;
    size_t bytes = sizeof(WInstCachedPlacement) + (size_t)count * sizeof(N2Mesh);
    if (!record || count <= 0) return 1;
    for (int i = before; i < scene->count; i++) {
        N2Mesh *m = &scene->meshes[i];
        if (!m->world_bounds_valid || m->shared_geometry) return 1;
        if (ground ? (m->cat != N2_ROAD && m->cat != N2_TERRAIN) : m->cat != N2_OTHER) return 1;
        /* Reserve room for persistent packed vertices as well as geometry. */
        bytes += sizeof(N2SharedGeometry) + (size_t)m->nidx * sizeof(uint16_t) +
            (size_t)m->nverts * ((m->authored_normals ? 8 : 5)*sizeof(float) +
                               (m->vcol ? 4 : 0) + (m->world_role == 1 ? 0 : 28));   /* 28 B packed vertex */
    }
    if (!winst_cache_make_room(bytes,tile_x,tile_y,ground)) return 1;
    WInstCachedPlacement *e = calloc(1,sizeof *e);
    if (!e) return 1;
    e->scene.meshes = calloc((size_t)count,sizeof(N2Mesh));
    if (!e->scene.meshes) { free(e); return 1; }
    for (int i = 0; i < count; i++) {
        N2Mesh *m = &scene->meshes[before+i];
        N2SharedGeometry *g = calloc(1,sizeof *g);
        if (!g) { winst_free_scene(&e->scene); free(e); return 1; }
        g->refs = 2; g->verts=m->verts; g->idx=m->idx; g->vcol=m->vcol;
        m->shared_geometry = g;
        e->scene.meshes[e->scene.count++] = *m;
    }
    e->record=record; e->bytes=bytes; e->ground=ground;
    e->tile_x=tile_x; e->tile_y=tile_y; e->touched=g_cache_epoch;
    e->lod_replacements=lod_replacements;
    e->lod_saved_indices=lod_saved_indices;
    unsigned slot=winst_cache_slot(record);
    e->next=g_placement_cache[slot]; g_placement_cache[slot]=e;
    g_placement_cache_bytes += bytes;
    if (ground) g_ground_cache_bytes+=bytes;
    return 1;
}

static int winst_cache_append_ground(WInstCachedPlacement *cached, N2Scene *dst,
                                    float x, float y, float radius, int own_matrix,
                                    WInstStats *stats) {
    cached->touched=g_cache_epoch;
    for (int k=0; k<cached->scene.count; k++) {
        N2Mesh m=cached->scene.meshes[k]; float *bb=m.world_bounds;
        float dx=x<bb[0]?bb[0]-x:x>bb[3]?x-bb[3]:0;
        float dy=y<bb[1]?bb[1]-y:y>bb[4]?y-bb[4]:0;
        if (radius>0 && dx*dx+dy*dy>radius*radius) continue;
        __atomic_add_fetch(&m.shared_geometry->refs,1,__ATOMIC_RELAXED);
        if (!winst_push_mesh(dst,m)) { n2_mesh_geometry_release(&m); return 0; }
        if (stats) { stats->meshes_placed++; if (own_matrix) stats->own_matrix_meshes++; }
        g_ground_cache_hits++;
    }
    return 1;
}
#endif

#ifdef __vita__
/* Low detail swaps scenery for its cheapest authored model -- often flat
 * cards meant for the distance. Placements within this range of the build
 * centre draw their full model instead (it is already placed, as the
 * collision copy); the stand-in is dropped. The car stays well inside it
 * between builds, and fog hides most of what lies beyond. */
#define WINST_LOD_NEAR_M 280.0f

/* Applies the near/far choice to the meshes one placement appended at
 * [before, count). Cached entries keep both versions; only this build's
 * copies change. */
static void winst_lod_resolve(N2Scene *scene, int before, const float matrix[16]) {
    int has_low = 0;
    for (int i = before; i < scene->count; i++) has_low |= scene->meshes[i].world_role == 2;
    if (!has_low) return;
    float dx = matrix[12] - g_cache_focus_x, dy = matrix[13] - g_cache_focus_y;
    int near = !g_winst_convert && dx*dx + dy*dy < WINST_LOD_NEAR_M*WINST_LOD_NEAR_M;
    int w = before;
    for (int i = before; i < scene->count; i++) {
        N2Mesh m = scene->meshes[i];
        if (near && m.world_role == 2) { n2_mesh_geometry_release(&m); continue; }
        if (near && m.world_role == 1) m.world_role = 0;
        /* converting: a tile decides near/far at load, and near draws this
           copy, so its prelight must survive (dropping it drew those models
           unshaded, too bright -- 10-09) */
        else if (!near && !g_winst_convert && m.world_role == 1 && m.shared_geometry) m.vcol = NULL;
        scene->meshes[w++] = m;
    }
    scene->count = w;
    if (near) g_lod_near_kept++;
}
#endif

static int winst_build_visit_inner(const WInstPlacement *placement,
                                   const char *type_name, void *userdata);
/* Every mesh a placement emits carries its scenery record. */
static int winst_build_visit(const WInstPlacement *placement,
                             const char *type_name, void *userdata) {
    WInstBuildVisit *b = userdata;
    int s0 = b->scene->count, v0 = b->vista->count;
    if (!winst_build_visit_inner(placement, type_name, userdata)) return 0;
    if (placement->region_id >= 0 && placement->region_id <= 0xFFFF &&
        placement->record_index >= 0 && placement->record_index <= 0xFFFF) {
        for (int i = s0; i < b->scene->count; i++) {
            N2Mesh *m = &b->scene->meshes[i];
            m->scen_has = 1; m->scen_region = (uint16_t)placement->region_id;
            m->scen_index = (uint16_t)placement->record_index;
        }
        for (int i = v0; i < b->vista->count; i++) {
            N2Mesh *m = &b->vista->meshes[i];
            m->scen_has = 1; m->scen_region = (uint16_t)placement->region_id;
            m->scen_index = (uint16_t)placement->record_index;
        }
    }
    return 1;
}
static int winst_build_visit_inner(const WInstPlacement *placement,
                                   const char *type_name, void *userdata) {
#ifdef __vita__
    WInstBuildVisit *build = userdata;
    if (world_build_aborted()) return 0;
    if (!g_placement_cache_bundle)
        return winst_build_visit_uncached(placement,type_name,userdata);
    unsigned slot = winst_cache_slot(placement->record_id);
    if (placement->record_id) for (WInstCachedPlacement *e = g_placement_cache[slot]; e; e=e->next) {
        if (e->ground || e->record != placement->record_id) continue;
        e->touched=g_cache_epoch;
        for (int i = 0; i < e->scene.count; i++) {
            N2Mesh m = e->scene.meshes[i];
            __atomic_add_fetch(&m.shared_geometry->refs, 1, __ATOMIC_RELAXED);
            if (!winst_push_mesh(build->scene, m)) { n2_mesh_geometry_release(&m); return 0; }
        }
        build->stats->meshes_placed += e->scene.count;
        build->stats->lod_replacements += e->lod_replacements;
        build->stats->lod_saved_indices += e->lod_saved_indices;
        g_cpu_cache_hits++;
        winst_lod_resolve(build->scene, build->scene->count - e->scene.count, placement->matrix);
        return 1;
    }
    int before = build->scene->count;
    long lod_before = build->stats->lod_replacements, saved_before = build->stats->lod_saved_indices;
    if (!winst_build_visit_uncached(placement,type_name,userdata)) return 0;
    g_cpu_cache_misses++;
    int stored = winst_cache_store(build->scene,before,placement->record_id,
        (int)floorf(placement->matrix[12]/128),(int)floorf(placement->matrix[13]/128),0,
        build->stats->lod_replacements-lod_before,build->stats->lod_saved_indices-saved_before);
    winst_lod_resolve(build->scene, before, placement->matrix);
    return stored;
#else
    return winst_build_visit_uncached(placement,type_name,userdata);
#endif
}

void world_instance_cache_selftest(void) {
#ifdef __vita__
    /* Production lookup and eviction, without depending on track files. */
    static const unsigned char identity[64] = {0};
    winst_cache_begin(identity,0,0,0,400);
    WInstCachedPlacement *e = calloc(1,sizeof *e);
    assert(e);
    e->record=identity; e->bytes=sizeof *e + sizeof(N2Mesh);
    e->scene.meshes=calloc(1,sizeof(N2Mesh)); assert(e->scene.meshes);
    e->scene.count=e->scene.cap=1;
    N2Mesh *m=&e->scene.meshes[0];
    m->verts=calloc(15,sizeof(float)); m->idx=calloc(3,sizeof(uint16_t));
    m->nverts=m->nidx=3; m->cat=N2_OTHER;
    m->shared_geometry=calloc(1,sizeof(N2SharedGeometry));
    assert(m->verts && m->idx && m->shared_geometry);
    m->shared_geometry->refs=1; m->shared_geometry->verts=m->verts;
    m->shared_geometry->idx=m->idx;
    unsigned slot=winst_cache_slot(identity);
    e->next=g_placement_cache[slot]; g_placement_cache[slot]=e;
    g_placement_cache_bytes+=e->bytes;
    N2Scene scene={0},vista={0}; WInstStats stats={0};
    WInstBuildVisit visit={NULL,&scene,&vista,&stats};
    WInstPlacement placement={0}; placement.record_id=identity;
    assert(winst_build_visit(&placement,"fixture",&visit));
    assert(scene.count==1 && scene.meshes[0].verts==m->verts);
    assert(m->shared_geometry->refs==2 && g_cpu_cache_hits==1);
    /* Retiring a resident must preserve the tile's data. */
    winst_free_scene(&scene); assert(m->shared_geometry->refs==1);
    assert(winst_build_visit(&placement,"fixture",&visit));
    /* Evicting a tile must preserve a resident's outstanding reference. */
    winst_cache_begin(identity,0,4096,4096,400);
    assert(g_placement_cache_bytes==0);
    assert(scene.meshes[0].shared_geometry->refs==1);
    scene.meshes[0].verts[0]=7; assert(scene.meshes[0].verts[0]==7);
    /* Simulate a full reservation with two small actual payloads. Incoming
       nearby data must evict the farther tile and preserve live residents. */
    for (int i=0; i<2; i++) {
        WInstCachedPlacement *v=calloc(1,sizeof *v); assert(v);
        v->record=identity+i; v->tile_x=i ? 3 : 0;
        v->bytes=WINST_SCENERY_CACHE_LIMIT/2;
        v->scene.meshes=malloc(sizeof(N2Mesh)); assert(v->scene.meshes);
        v->scene.count=v->scene.cap=1;
        v->scene.meshes[0]=scene.meshes[0];
        __atomic_add_fetch(&v->scene.meshes[0].shared_geometry->refs,1,__ATOMIC_RELAXED);
        unsigned vs=winst_cache_slot(v->record);
        v->next=g_placement_cache[vs]; g_placement_cache[vs]=v;
        g_placement_cache_bytes+=v->bytes;
    }
    winst_cache_begin(identity,0,0,0,400);
    assert(winst_cache_make_room(WINST_GROUND_CACHE_LIMIT,0,0,1));
    assert(g_cache_evicted==0);
    assert(winst_cache_make_room(1024*1024,0,0,0));
    assert(g_cache_evicted==1 && g_placement_cache_bytes==WINST_SCENERY_CACHE_LIMIT/2);
    assert(scene.meshes[0].shared_geometry->refs==2);
    /* A request farther away must not displace the closer remaining tile. */
    assert(!winst_cache_make_room(WINST_SCENERY_CACHE_LIMIT/2+1,4,0,0));
    assert(!winst_cache_make_room(WINST_GROUND_CACHE_LIMIT+1,0,0,1));
    winst_cache_begin(NULL,0,0,0,0);
    assert(scene.meshes[0].shared_geometry->refs==1);
    N2Mesh *terrain=&scene.meshes[0]; terrain->cat=N2_TERRAIN;
    terrain->world_bounds_valid=1;
    float ground_bounds[6]={0,0,0,2,2,0};
    memcpy(terrain->world_bounds,ground_bounds,sizeof ground_bounds);
    n2_mesh_bias_terrain(terrain);
    float height=terrain->verts[2]; n2_mesh_bias_terrain(terrain);
    assert(height==-0.05f && terrain->verts[2]==height && terrain->world_bounds[2]==height);
    N2Mesh ground_samples[2]={*terrain,*terrain};
    ground_samples[1].world_bounds[0]=ground_samples[1].world_bounds[1]=1000;
    ground_samples[1].world_bounds[3]=ground_samples[1].world_bounds[4]=1002;
    __atomic_add_fetch(&terrain->shared_geometry->refs,2,__ATOMIC_RELAXED);
    WInstCachedPlacement ground={0}; ground.scene=(N2Scene){ground_samples,2,2};
    N2Scene clipped={0}; WInstStats ground_stats={0};
    assert(winst_cache_append_ground(&ground,&clipped,0,0,10,1,&ground_stats));
    assert(clipped.count==1 && clipped.meshes[0].world_bounds[0]==0);
    n2_mesh_bias_terrain(&clipped.meshes[0]); assert(clipped.meshes[0].verts[2]==height);
    winst_free_scene(&clipped);
    assert(winst_cache_append_ground(&ground,&clipped,1000,1000,10,1,&ground_stats));
    assert(clipped.count==1 && clipped.meshes[0].world_bounds[0]==1000);
    assert(ground_stats.meshes_placed==2 && ground_stats.own_matrix_meshes==2);
    winst_free_scene(&clipped);
    n2_mesh_geometry_release(&ground_samples[0]); n2_mesh_geometry_release(&ground_samples[1]);
    assert(terrain->shared_geometry->refs==1);
    winst_free_scene(&scene);
#endif
}

/* winst_collect_model assigns one category to a whole object, so a prototype
 * is ground or it is not. */
static int winst_proto_is_ground(const WInstProto *proto) {
    return proto->any_ground;
}

typedef struct {
    WInstLibrary *library;
    uint32_t *ground_keys;   /* sorted; gates the walk away from the library scan */
    int ground_key_count;
} WInstOwnership;

static int winst_cmp_key(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static int winst_ownership_open(WInstOwnership *own, WInstLibrary *library) {
    memset(own, 0, sizeof *own);
    own->library = library;
    if (library->count <= 0) return 1;
    own->ground_keys = (uint32_t *)malloc((size_t)library->count *
                                          sizeof *own->ground_keys);
    if (!own->ground_keys) return 0;
    for (int i = 0; i < library->count; i++)
        if (library->items[i].model_key &&
            winst_proto_is_ground(&library->items[i]))
            own->ground_keys[own->ground_key_count++] = library->items[i].model_key;
    qsort(own->ground_keys, (size_t)own->ground_key_count,
          sizeof *own->ground_keys, winst_cmp_key);
    return 1;
}

/* Ownership pre-pass. Scanned unfiltered on purpose: an instance outside the
 * view radius, or hidden by an event's scenery selection, still has to suppress
 * the identity copy rather than leave it stranded at the prototype origin. */
static int winst_mark_instanced(const WInstPlacement *placement,
                                const char *type_name, void *userdata) {
    WInstOwnership *own = (WInstOwnership *)userdata;
    WInstLibrary *library = own->library;
    if (winst_identity_placement(placement->matrix)) return 1;
    int keyed = 0, ground = 0;
    for (int lod = 0; lod < 3; lod++) {
        uint32_t key = placement->model_keys[lod];
        if (!key) continue;
        keyed = 1;
        if (bsearch(&key, own->ground_keys, (size_t)own->ground_key_count,
                    sizeof *own->ground_keys, winst_cmp_key)) ground = 1;
    }
    if (keyed) {
        if (!ground) return 1;
        /* Claim every authored LOD alternative and every duplicated copy of
         * this object, not only the one the resolver picks. The bundle carries
         * 1a/1b/1z models of the same object and repeats them per region; an
         * unclaimed sibling is still emitted at the prototype origin. */
        for (int lod = 0; lod < 3; lod++) {
            uint32_t key = placement->model_keys[lod];
            if (!key) continue;
            for (int i = 0; i < library->count; i++)
                if (library->items[i].model_key == key)
                    library->items[i].instanced = 1;
        }
        return 1;
    }
    /* Records carrying no authored key at all are matched by name, exactly as
     * winst_library_resolve falls back to doing. */
    char folded[28];
    uint32_t hash = winst_name_key(type_name, folded);
    for (int i = 0; i < library->count; i++)
        if (library->items[i].name_hash == hash &&
            !strcmp(library->items[i].name, folded) &&
            winst_proto_is_ground(&library->items[i]))
            library->items[i].instanced = 1;
    return 1;
}

/* XY distance from (x,y) to a mesh's bounds after `matrix` (column-major,
   as winst_place_mesh applies it). */
static float winst_mesh_dist2(const N2Mesh *m, const float *matrix, float x, float y,
                              float out_bb[4]) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (int v = 0; v < m->nverts; v++) {
        const float *p = m->verts + v*5;
        float wx = matrix[0]*p[0] + matrix[4]*p[1] + matrix[8]*p[2] + matrix[12];
        float wy = matrix[1]*p[0] + matrix[5]*p[1] + matrix[9]*p[2] + matrix[13];
        if (wx < x0) x0 = wx; if (wx > x1) x1 = wx;
        if (wy < y0) y0 = wy; if (wy > y1) y1 = wy;
    }
    out_bb[0] = x0; out_bb[1] = y0; out_bb[2] = x1; out_bb[3] = y1;
    if (x0 > x1) return 1e30f;
    float dx = x < x0 ? x0 - x : x > x1 ? x - x1 : 0.0f;
    float dy = y < y0 ? y0 - y : y > y1 ? y - y1 : 0.0f;
    return dx*dx + dy*dy;
}

#ifdef __vita__
/* The pinned bundle never changes, so neither does the library order: the
   placed ground bounds of prototype i are remembered across builds and far
   prototypes are skipped without being parsed. Builds run one at a time. */
static float (*g_ground_bb)[4];   /* x0 y0 x1 y1; x0 > x1 = not measured */
static int g_ground_bb_n;
static signed char *own_memo; static const void *own_memo_items; static int own_memo_n;
#endif

static int winst_place_ground_prototypes(const WInstLibrary *library,
                                         N2Scene *scene, N2Scene *vista,
                                         WInstStats *stats,
                                         float focus_x, float focus_y, float radius) {
    static const float identity[16] = {
        1, 0, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 0, 0, 0, 0, 1,
    };
    if (!library || !scene || !vista) return 0;
#ifdef __vita__
    if (g_ground_bb_n != library->count) {
        free(g_ground_bb);
        g_ground_bb = (float (*)[4])malloc((size_t)library->count * sizeof *g_ground_bb);
        g_ground_bb_n = g_ground_bb ? library->count : 0;
        for (int i = 0; i < g_ground_bb_n; i++) { g_ground_bb[i][0] = 1.0f; g_ground_bb[i][2] = 0.0f; }
    }
#endif
    for (int i = 0; i < library->count; i++) {
        WInstProto *proto = (WInstProto *)&library->items[i];
        if (proto->instanced || !proto->any_ground) continue;
#ifdef __vita__
        /* before anything costly: winst_proto_has_own_matrix scans every
           prototype name, which over the whole library was seconds a build */
        float *gb = g_ground_bb && !proto->is_vista && radius > 0.0f ? g_ground_bb[i] : NULL;
        if (gb && gb[0] <= gb[2]) {
            float dx = focus_x < gb[0] ? gb[0]-focus_x : focus_x > gb[2] ? focus_x-gb[2] : 0.0f;
            float dy = focus_y < gb[1] ? gb[1]-focus_y : focus_y > gb[3] ? focus_y-gb[3] : 0.0f;
            if (dx*dx + dy*dy > radius*radius) continue;   /* wholly out of range */
        }
#endif
#ifdef __vita__
        /* Position independent, and a scan of every prototype name per call
           (~1300 ground prototypes x 10698 names a build): remember it for
           the cached library. */
        if (own_memo_items != (const void *)library->items || own_memo_n != library->count) {
            free(own_memo);
            own_memo = (signed char *)malloc((size_t)(library->count ? library->count : 1));
            if (own_memo) memset(own_memo, -1, (size_t)(library->count ? library->count : 1));
            own_memo_items = library->items; own_memo_n = own_memo ? library->count : 0;
        }
        int own_matrix;
        if (own_memo && i < own_memo_n) {
            if (own_memo[i] < 0) own_memo[i] = (signed char)winst_proto_has_own_matrix(library, proto);
            own_matrix = own_memo[i];
        } else own_matrix = winst_proto_has_own_matrix(library, proto);
#else
        int own_matrix = winst_proto_has_own_matrix(library, proto);
#endif
        const float *matrix = own_matrix ? proto->matrix : identity;
        N2Scene *dst = proto->is_vista ? vista : scene;
#ifdef __vita__
        const unsigned char *cache_key=library->data+proto->begin;
        WInstCachedPlacement *cached=NULL;
        if (g_placement_cache_bundle && dst==scene)
            for (WInstCachedPlacement *e=g_placement_cache[winst_cache_slot(cache_key)]; e; e=e->next)
                if (e->ground && e->record==cache_key) { cached=e; break; }
        if (cached) {
            if (!winst_cache_append_ground(cached,dst,focus_x,focus_y,radius,own_matrix,stats))
                return 0;
            continue;
        }
        int ground_before=dst->count, ground_total=0;
#endif
        /* Placed exactly once: parse, copy into the scene, release. */
        const N2Scene *meshes = winst_proto_load(library, proto);
        int ok = 1;
        for (int mesh = 0; mesh < meshes->count && ok; mesh++) {
            const N2Mesh *source = &meshes->meshes[mesh];
            if (source->cat != N2_ROAD && source->cat != N2_TERRAIN) continue;
#ifdef __vita__
            ground_total++;
            /* Only this resident's ground: the whole city's terrain in every
               resident is what kept two residents from fitting in memory. */
            if (dst == scene && radius > 0.0f) {
                float mb[4];
                float d2 = winst_mesh_dist2(source, matrix, focus_x, focus_y, mb);
                if (gb && mb[0] <= mb[2]) {          /* grow the remembered bounds */
                    if (gb[0] > gb[2]) { memcpy(gb, mb, sizeof mb); }
                    else { if (mb[0] < gb[0]) gb[0] = mb[0]; if (mb[1] < gb[1]) gb[1] = mb[1];
                           if (mb[2] > gb[2]) gb[2] = mb[2]; if (mb[3] > gb[3]) gb[3] = mb[3]; }
                }
                if (d2 > radius * radius) continue;
            }
#endif
            if (!winst_place_mesh(dst, source, matrix, proto->name, stats)) ok = 0;
            else if (own_matrix && stats) stats->own_matrix_meshes++;
        }
#ifdef __vita__
        /* Admit only complete prototype ground sets; a clipped set cannot
           stand in for this prototype when the next focus moves. */
        if (ok && g_placement_cache_bundle && dst==scene && ground_total>0 &&
            dst->count-ground_before==ground_total) {
            float mn[2]={1e30f,1e30f},mx[2]={-1e30f,-1e30f};
            for (int k=ground_before; k<dst->count; k++) {
                N2Mesh *m=&dst->meshes[k]; n2_mesh_bias_terrain(m);
                for (int c=0; c<2; c++) {
                    if (m->world_bounds[c]<mn[c]) mn[c]=m->world_bounds[c];
                    if (m->world_bounds[c+3]>mx[c]) mx[c]=m->world_bounds[c+3];
                }
            }
            winst_cache_store(dst,ground_before,cache_key,
                (int)floorf((mn[0]+mx[0])*0.5f/128),
                (int)floorf((mn[1]+mx[1])*0.5f/128),1,0,0);
        }
#endif
        winst_proto_unload(proto);
        if (!ok) return 0;
    }
    return 1;
}

static int winst_move_regions(WInstRegion **all, int *count, int *cap,
                              WInstRegion *add, int nadd) {
    if (nadd < 0 || *count > INT_MAX - nadd) return 0;
    int need = *count + nadd;
    if (need > *cap) {
        int next = *cap ? *cap : 64;
        while (next < need) {
            if (next > INT_MAX / 2) { next = need; break; }
            next *= 2;
        }
        if ((size_t)next > SIZE_MAX / sizeof **all) return 0;
        WInstRegion *grown = (WInstRegion *)realloc(
            *all, (size_t)next * sizeof **all);
        if (!grown) return 0;
        *all = grown;
        *cap = next;
    }
    memcpy(*all + *count, add, (size_t)nadd * sizeof *add);
    *count = need;
    free(add);
    return 1;
}

static int winst_has_suffix(const char *text, const char *suffix) {
    size_t n = strlen(text), s = strlen(suffix);
    return n >= s && !strcmp(text + n - s, suffix);
}

/* Bundles the caller already holds in memory (world.c keeps every region's
 * STREAM*.BUN resident for this whole build). Borrowing them instead of
 * re-reading avoids a second ~113 MB copy of the same file, which is the
 * difference between fitting and not fitting on low-memory targets. */
#define WINST_MAX_LENT 8
static struct { char name[64]; const unsigned char *data; long len; } winst_lent[WINST_MAX_LENT];
static int winst_nlent;

void world_instance_lend_bundle(const char *name, const unsigned char *data, long len) {
    if (!name) { winst_nlent = 0; return; }
    if (!data || len <= 0 || winst_nlent >= WINST_MAX_LENT) return;
    snprintf(winst_lent[winst_nlent].name, sizeof winst_lent[0].name, "%s", name);
    winst_lent[winst_nlent].data = data;
    winst_lent[winst_nlent].len = len;
    winst_nlent++;
}

static const unsigned char *winst_borrow(const char *name, long *len) {
    for (int i = 0; i < winst_nlent; i++) {
        size_t n = strlen(winst_lent[i].name);
        if (!strncmp(name, winst_lent[i].name, n) && (!name[n] || !strcmp(name + n, ".BUN"))) {
            *len = winst_lent[i].len;
            return winst_lent[i].data;
        }
    }
    return NULL;
}

static unsigned char *winst_read_named(const char *root, const char *name,
                                       long *len) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s%s", root, name,
             winst_has_suffix(name, ".BUN") ? "" : ".BUN");
#ifdef __vita__
    /* The region companion (e.g. L4RA.BUN, ~420 KB) was read from the card
       twice per streaming build. Keep the last two small files; callers own
       and free the copy they get. Builds run one at a time on the worker. */
    static struct { char path[256]; unsigned char *data; long len; } keep[2];
    static int keep_next;
    for (int i = 0; i < 2; i++)
        if (keep[i].data && !strcmp(keep[i].path, path)) {
            unsigned char *copy = (unsigned char *)malloc((size_t)keep[i].len);
            if (!copy) break;
            memcpy(copy, keep[i].data, (size_t)keep[i].len);
            if (len) *len = keep[i].len;
            return copy;
        }
    long got = 0;
    unsigned char *data = n2_read_file(path, &got);
    if (len) *len = got;
    if (data && got > 0 && got <= 2 * 1024 * 1024 && strlen(path) < sizeof keep[0].path) {
        int k = keep_next; keep_next ^= 1;
        free(keep[k].data);
        keep[k].data = (unsigned char *)malloc((size_t)got);
        if (keep[k].data) {
            memcpy(keep[k].data, data, (size_t)got); keep[k].len = got;
            snprintf(keep[k].path, sizeof keep[k].path, "%s", path);
        }
    }
    return data;
#else
    return n2_read_file(path, len);
#endif
}

static int winst_prepare_combined(const N2Scene *dst, N2Scene *add,
                                  N2Mesh **combined, int *count) {
    if (!dst || !add || !combined || !count || dst->count < 0 || add->count < 0 ||
        dst->count > INT_MAX - add->count) return 0;
    *count = dst->count + add->count;
    *combined = NULL;
    if (!*count) return 1;
    if ((size_t)*count > SIZE_MAX / sizeof **combined) return 0;
    /* Merge inside the freshly built array (it doubled while growing, so it
       usually has room already) instead of asking for a second block of the
       same size: on a long-running, fragmented heap that second 2+ MB block
       is what failed, every build, until the car ran off the loaded area. */
    if (add->meshes && add->count) {
        N2Mesh *room = add->meshes;
        if (add->cap < *count)
            room = (N2Mesh *)realloc(add->meshes, (size_t)*count * sizeof **combined);
        if (room) {
            if (dst->count) {
                memmove(room + dst->count, room, (size_t)add->count * sizeof *room);
                memcpy(room, dst->meshes, (size_t)dst->count * sizeof *room);
            }
            if (add->cap > *count) {   /* hand back the doubling slack */
                N2Mesh *fit = (N2Mesh *)realloc(room, (size_t)*count * sizeof *room);
                if (fit) room = fit;
            }
            add->meshes = NULL; add->count = add->cap = 0;   /* now owned by *combined */
            *combined = room;
            return 1;
        }
    }
    *combined = (N2Mesh *)malloc((size_t)*count * sizeof **combined);
    if (!*combined) return 0;
    if (dst->count)
        memcpy(*combined, dst->meshes, (size_t)dst->count * sizeof **combined);
    if (add->count)
        memcpy(*combined + dst->count, add->meshes,
               (size_t)add->count * sizeof **combined);
    return 1;
}

static int winst_commit_scenes(N2Scene *scene, N2Scene *vista,
                               N2Scene *add_scene, N2Scene *add_vista) {
    N2Mesh *combined_scene = NULL, *combined_vista = NULL;
    int scene_count = 0, vista_count = 0;
    if (!winst_prepare_combined(scene, add_scene, &combined_scene, &scene_count) ||
        !winst_prepare_combined(vista, add_vista, &combined_vista, &vista_count)) {
        free(combined_scene);
        free(combined_vista);
        return 0;
    }
    free(scene->meshes);
    free(vista->meshes);
    free(add_scene->meshes);
    free(add_vista->meshes);
    scene->meshes = combined_scene;
    scene->count = scene->cap = scene_count;
    vista->meshes = combined_vista;
    vista->count = vista->cap = vista_count;
    memset(add_scene, 0, sizeof *add_scene);
    memset(add_vista, 0, sizeof *add_vista);
    return 1;
}

/* Validate every override before filtering, including records outside focus.
 * Missing/duplicate sections, stale flags or row indices cannot hide scenery. */
static int winst_check_scenery(const unsigned char *data,long begin,long end,
                               unsigned depth,WGSelection *selection) {
    if(depth>64)return 0;
    for(long p=begin;p<end;) {
        long next;
        if(end-p<8 || !chunk_end(p+8,n2_u32(data+p+4),end,&next))return 0;
        uint32_t id=n2_u32(data+p);
        if(id==0x80034100u) {
            WInstSection s;
            if(!winst_parse_section(data,p+8,next,&s))return 0;
            for(size_t i=0;i<selection->count;i++) {
                WGSelected *item=selection->items+i;
                if(item->section!=s.region_id)continue;
                if(item->checked || item->row>=s.placement_count)return 0;
                const unsigned char *r=s.placements+64L*item->row;
                if(winst_u16(r+0x1a)!=item->flags || winst_u16(r+0x18)>=s.type_count)return 0;
                item->checked=1;
            }
        } else if((id>>28)==8 && !winst_check_scenery(data,p+8,next,depth+1,selection))return 0;
        p=next;
    }
    return 1;
}

#ifdef __vita__
/* The prototype library indexes the pinned bundle, which never changes, so it
   is built once and reused by every resident build (builds run one at a
   time). Per-build state -- parsed prototype geometry -- is released at the
   end of each build; the index and its texture keys stay. */
static WInstLibrary g_lib;
static uint32_t *g_lib_keys;
static const unsigned char *g_lib_data;
static int g_lib_owned;   /* ownership flags already marked in g_lib */
/* World tiles in use: the prototype library (built by a conversion) is never
   needed again and points into the full bundle about to be stripped. */
void world_instance_library_release(void) {
    for (int i = 0; i < g_lib.count; i++)
        if (g_lib.items[i].loaded) winst_proto_unload(&g_lib.items[i]);
    winst_library_free(&g_lib);
    free(g_lib_keys); g_lib_keys = NULL; g_lib_data = NULL; g_lib_owned = 0;
    /* keyed by library size / item pointer: stale once another bundle's
       library takes the same shape or address */
    free(g_ground_bb); g_ground_bb = NULL; g_ground_bb_n = 0;
    free(own_memo); own_memo = NULL; own_memo_items = NULL; own_memo_n = 0;
}
#endif

int world_instance_build_for_event(N2Scene *scene, N2Scene *vista,
                         const char *track_root,
                         const char *const *bundles, int bundle_count,
                         float focus_x, float focus_y, float view_radius,
                         const unsigned char *shared, long shared_len,
                         WInstStats *stats, int scenery_event) {
    WInstStats local_stats;
    WInstRegion *regions = NULL;
    int region_count = 0, region_cap = 0;
    unsigned char *selected = NULL;
    const unsigned char *bundle_data = NULL;
    long bundle_len = 0;
    int bundle_borrowed = 0;
    uint32_t *keys = NULL;
    WInstLibrary library;
    N2Scene built_scene, built_vista;
    WGSelection scenery = {0};
    int ok = 0;
    memset(&local_stats, 0, sizeof local_stats);
    local_stats.home_region = -1;
    memset(&library, 0, sizeof library);
    memset(&built_scene, 0, sizeof built_scene);
    memset(&built_vista, 0, sizeof built_vista);
    if (stats) *stats = local_stats;
    if (!scene || !vista || !track_root || !bundles || bundle_count <= 0 ||
        !isfinite(focus_x) || !isfinite(focus_y) || !isfinite(view_radius) ||
        view_radius < 0.0f || shared_len < 0 ||
        scenery_event < -1 || scenery_event > 65535) goto cleanup;

    for (int i = 0; i < bundle_count; i++) {
        const char *bundle = bundles[i];
        if (!bundle || !bundle[0] || !strcmp(bundle, "ALL")) goto cleanup;
        const char *stem = !strncmp(bundle, "STREAM", 6) ? bundle + 6 : bundle;
        char companion[64];
        snprintf(companion, sizeof companion, "%s", stem);
        char *dot = strrchr(companion, '.');
        if (dot && !strcmp(dot, ".BUN")) *dot = 0;
        long companion_len = 0;
        unsigned char *companion_data = winst_read_named(track_root, companion,
                                                         &companion_len);
        if (!companion_data) goto cleanup;
        WInstRegion *part = NULL;
        int npart = 0;
        int parsed = winst_parse_regions(companion_data, companion_len,
                                         &part, &npart);
        free(companion_data);
        if (!parsed || !winst_move_regions(&regions, &region_count, &region_cap,
                                           part, npart)) {
            winst_free_regions(part, npart);
            goto cleanup;
        }
    }

    selected = (unsigned char *)calloc(65536, 1);
    if (!selected) goto cleanup;
    int home_index = -1;
    int nselected = winst_select_regions(regions, region_count, focus_x, focus_y,
                                         view_radius, selected, 65536, &home_index);
    local_stats.regions_total = region_count;
    local_stats.regions_selected = nselected;
    if (home_index < 0 && g_winst_convert && nselected > 0) {
        /* converting: a cell centre over water still owns the land at its
           edges; borrow the nearest selected district as home */
        float bd = 1e30f;
        for (int i = 0; i < region_count; i++) {
            int id = regions[i].id;
            if (id < 0 || id >= 65536 || !selected[id]) continue;
            float dx = focus_x < regions[i].bb[0] ? regions[i].bb[0] - focus_x
                     : focus_x > regions[i].bb[2] ? focus_x - regions[i].bb[2] : 0.0f;
            float dy = focus_y < regions[i].bb[1] ? regions[i].bb[1] - focus_y
                     : focus_y > regions[i].bb[3] ? focus_y - regions[i].bb[3] : 0.0f;
            if (dx*dx + dy*dy < bd) { bd = dx*dx + dy*dy; home_index = i; }
        }
    }
    if (home_index < 0 || home_index >= region_count) goto cleanup;
    local_stats.home_region = regions[home_index].id;
    VMARK("    regions: %d read, %d selected", region_count, nselected);

    for (int i = 0; i < bundle_count && !bundle_data; i++) {
        long candidate_len = 0;
        const unsigned char *candidate = winst_borrow(bundles[i], &candidate_len);
        int borrowed = candidate != NULL;
        if (!candidate) candidate = winst_read_named(track_root, bundles[i], &candidate_len);
        if (!candidate) continue;
        WInstWalk find;
        memset(&find, 0, sizeof find);
        find.find_region = local_stats.home_region;
        int walked = winst_walk_sections(candidate, 0, candidate_len, &find);
        if (walked && find.found_region) {
            bundle_data = candidate;
            bundle_len = candidate_len;
            bundle_borrowed = borrowed;
            snprintf(local_stats.bundle, sizeof local_stats.bundle, "%s", bundles[i]);
        } else if (!borrowed) free((void *)candidate);
    }
    /* The home section is the atomic gate: destination scenes are untouched
     * until a complete temporary assembly has been built and committed. */
    if (!bundle_data) goto cleanup;
    VMARK("    home section found");

    /* A requested event id the bundle does not author is degraded to the
     * unfiltered scene. That is an authoring fact, not a corrupt table, so it
     * must not fail the build: L4RB 4201/4202/4203 and L4RC 4341 are raceable
     * events with no group of their own. Everything else still fails closed. */
    int effective_event = scenery_event;
    if (scenery_event) {
        const char *stem = !strncmp(local_stats.bundle,"STREAM",6)
                         ? local_stats.bundle+6 : local_stats.bundle;
        long len=0;unsigned char *data=winst_read_named(track_root,stem,&len);
        WGTable table;
        int valid=data && wg_open_file(data,(size_t)len,&table);
        if(valid && !wg_event_group_present(&table,scenery_event))effective_event=0;
        valid=valid && (!effective_event ||
                        wg_selection_open(&table,effective_event,&scenery));
        free(data); /* selection owns copied membership, not borrowed bytes */
        if(!valid)goto cleanup;
        if(effective_event) {
            if(!winst_check_scenery(bundle_data,0,bundle_len,0,&scenery))goto cleanup;
            for(size_t i=0;i<scenery.count;i++)if(!scenery.items[i].checked)goto cleanup;
        }
    }
    local_stats.scenery_effective = effective_event;

#ifdef __vita__
    int lib_cached = g_lib.items && g_lib_data == bundle_data;
    if (lib_cached) {
        library = g_lib;
        VMARK("    library reused: %d prototypes", library.count);
    } else {
#endif
    keys = (uint32_t *)malloc(16384 * sizeof *keys);
    if (!keys) goto cleanup;
    N2Tpk tpk = n2_tpk_open(bundle_data, bundle_len);
    int nkeys = n2_tpk_keys(bundle_data, tpk, keys, 16384);
    if (shared && nkeys < 16384)
        nkeys += n2_car_tex_keys(shared, shared_len, keys + nkeys, 16384 - nkeys);
    /* Instance prototypes can reference shared gameplay textures that are
     * authored only in GLOBAL/InGameCommon.bun. Those keys must participate
     * in per-submesh resolution; otherwise the affected ranges lose exact
     * material ownership and are conservatively rendered as opaque. */
    if (nkeys < 16384) {
        char path[1024];
        int length = snprintf(path, sizeof path,
                              "%s/../GLOBAL/InGameCommon.bun", track_root);
        if (length >= 0 && (size_t)length < sizeof path) {
            long common_len = 0;
            unsigned char *common = n2_read_file(path, &common_len);
            if (common) {
                N2Tpk common_tpk = n2_tpk_open(common, common_len);
                nkeys += n2_tpk_keys(common, common_tpk, keys + nkeys,
                                     16384 - nkeys);
                free(common_tpk.blk);
                free(common);
            }
        }
    }
    if (nkeys < 16384) {
        long ads_len = 0;
        unsigned char *ads = n2_read_global_ads(track_root, &ads_len);
        if (ads) {
            N2Tpk ads_tpk = n2_tpk_open(ads, ads_len);
            nkeys += n2_tpk_keys(ads, ads_tpk, keys + nkeys, 16384 - nkeys);
            free(ads_tpk.blk); free(ads);
        }
    }
    /* Prototype geometry is re-parsed on first use, so the library keeps
     * borrowing the bundle and the key set until the build is done. */
    library.data = bundle_data;
    library.keys = keys;
    library.nkeys = nkeys;
    VMARK("    library scan start");
    winst_collect_models(&library, bundle_data, 0, bundle_len, keys, nkeys);
    winst_library_index(&library);
    VMARK("    library scan done: %d prototypes", library.count);
    VMARK("    model lookup: %u slots (%u KB)", library.model_index_cap,
          (unsigned)(library.model_index_cap * sizeof(int) / 1024));
#ifdef __vita__
    free(tpk.blk);
    g_lib = library; g_lib_keys = keys; g_lib_data = bundle_data;
    keys = NULL;          /* owned by the cache now */
    lib_cached = 1;
    }
#endif

    WInstOwnership owner;
    WInstWalk ownership;
#ifdef __vita__
    /* The ownership pass is position independent (whole bundle, unfiltered)
       and only sets flags in the library, which is cached: do it once. */
    if (!lib_cached) g_lib_owned = 0;
    if (g_lib_owned) {
        VMARK("    ownership reused");
    } else {
#endif
    if (!winst_ownership_open(&owner, &library)) goto cleanup;
    memset(&ownership, 0, sizeof ownership);
    ownership.find_region = -1;
    ownership.view_radius = WINST_WORLD_LIMIT;
    ownership.visit = winst_mark_instanced;
    ownership.userdata = &owner;
    int owned = winst_walk_sections(bundle_data, 0, bundle_len, &ownership);
    free(owner.ground_keys);
    if (!owned) goto cleanup;
#ifdef __vita__
    g_lib_owned = 1;
    }
    if (world_build_aborted()) goto cleanup;
#endif

    VMARK("    ownership done; ground placement start");
#ifdef __vita__
    winst_cache_begin(bundle_borrowed ? bundle_data : NULL, effective_event,
                      focus_x, focus_y, view_radius);
#endif
    /* margin: ground just past the resident edge still catches a car the
       swap was late for */
    if (!winst_place_ground_prototypes(&library, &built_scene, &built_vista,
                                       &local_stats, focus_x, focus_y,
                                       view_radius + 60.0f)) goto cleanup;
    VMARK("    ground placed: %d meshes", built_scene.count);
#ifdef __vita__
    VMARK("    CPU tiles: %ld ground meshes reused",g_ground_cache_hits);
#endif
#ifdef __vita__
    if (world_build_aborted()) goto cleanup;
#endif

    WInstBuildVisit visit = { &library, &built_scene, &built_vista, &local_stats };
    WInstWalk collect;
    memset(&collect, 0, sizeof collect);
    /* Companion polygons locate home and choose this one bundle, but are not a
     * complete ownership index. Scan its sections and filter each record by
     * authored bounds so nearby geometry from unmapped sections is retained. */
    collect.find_region = local_stats.home_region;
    collect.focus_x = focus_x;
    collect.focus_y = focus_y;
    collect.view_radius = view_radius;
    collect.visit = winst_build_visit;
    collect.userdata = &visit;
    collect.stats = &local_stats;
    collect.scenery = effective_event ? &scenery : NULL;
#ifdef __vita__
    g_visit_ticks = 0;
    Uint64 walk_t0 = SDL_GetPerformanceCounter();
#endif
    if (!winst_walk_sections(bundle_data, 0, bundle_len, &collect) ||
        !collect.found_region) goto cleanup;
    VMARK("    objects placed: %d meshes", built_scene.count);
#ifdef __vita__
    {   double f = (double)SDL_GetPerformanceFrequency(), all = (double)(SDL_GetPerformanceCounter() - walk_t0);
        VMARK("    objects: %ld records scanned, %ld in range; scan %.0f ms, placing %.0f ms",
              local_stats.instances_seen, local_stats.instances_in_range,
              (all - (double)g_visit_ticks) * 1000.0 / f, (double)g_visit_ticks * 1000.0 / f); }
#endif
#ifdef __vita__
    VMARK("    CPU tiles: %ld placements reused, %ld built, %u KB cache reserved",
          g_cpu_cache_hits, g_cpu_cache_misses, (unsigned)(g_placement_cache_bytes/1024));
    VMARK("    CPU tiles: %ld distant placements evicted", g_cache_evicted);
    VMARK("    CPU cache split: ground %u / 8192 KB, scenery %u / 16384 KB",
          (unsigned)(g_ground_cache_bytes/1024),
          (unsigned)((g_placement_cache_bytes-g_ground_cache_bytes)/1024));
#endif
    long render_indices = 0, collision_meshes = 0;
    for (int i = 0; i < built_scene.count; i++) {
        if (built_scene.meshes[i].world_role == 1) collision_meshes++;
        else render_indices += built_scene.meshes[i].nidx;
    }
#ifdef __vita__
    VMARK("    detail %s: %ld replacements (%ld drawn full within %.0f m), %ld triangles saved, %ld render triangles, %ld collision-only meshes",
          g_world_low_detail ? "low" : "full", local_stats.lod_replacements, g_lod_near_kept,
          WINST_LOD_NEAR_M, local_stats.lod_saved_indices / 3, render_indices / 3, collision_meshes);
#else
    VMARK("    detail %s: %ld replacements, %ld triangles saved, %ld render triangles, %ld collision-only meshes",
          g_world_low_detail ? "low" : "full", local_stats.lod_replacements,
          local_stats.lod_saved_indices / 3, render_indices / 3, collision_meshes);
#endif
    if (!winst_commit_scenes(scene, vista, &built_scene, &built_vista)) goto cleanup;
    ok = 1;

cleanup:
    free(scenery.items);
    if (!ok) {
        winst_free_scene(&built_scene);
        winst_free_scene(&built_vista);
    }
#ifdef __vita__
    if (g_lib.items && library.items == g_lib.items) {
        for (int i = 0; i < library.count; i++)
            if (library.items[i].loaded) winst_proto_unload(&library.items[i]);
        g_lib = library;
        memset(&library, 0, sizeof library);
    }
#endif
    winst_library_free(&library);
    free(keys);
    if (!bundle_borrowed) free((void *)bundle_data);
    free(selected);
    winst_free_regions(regions, region_count);
    if (stats) *stats = local_stats;
    return ok;
}

int world_instance_build(N2Scene *scene, N2Scene *vista,
                         const char *track_root,
                         const char *const *bundles, int bundle_count,
                         float focus_x, float focus_y, float view_radius,
                         const unsigned char *shared, long shared_len,
                         WInstStats *stats) {
    return world_instance_build_for_event(scene, vista, track_root, bundles, bundle_count,
                                focus_x, focus_y, view_radius, shared, shared_len, stats, 0);
}
