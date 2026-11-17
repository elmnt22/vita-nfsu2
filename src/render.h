/* render.h — OpenUG2 Renderer module: GL context-level state (shaders, buffers,
 * textures), the tiny column-major matrix library, the 3x5 bitmap font and the
 * dev PNG screenshot writer. Owns every gl* call that creates GPU objects;
 * main.c only binds/draws through GpuMesh + the RProg uniform handles.
 *
 * Also the single home of the GL headers: desktop legacy GL 2.1 + GLSL 120, or
 * OpenGL ES 2.0 + GLSL 100 with -DN2_GLES — every module includes GL via here. */
#ifndef OPENUG2_RENDER_H
#define OPENUG2_RENDER_H

#include <SDL.h>
#ifdef N2_GLES
#  include <SDL_opengles2.h>
#elif defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION 1
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#else
#  define GL_GLEXT_PROTOTYPES 1
#  include <SDL_opengl.h>
#endif

#include "nfsu2.h"

/* per-mesh GPU buffers + computed normals */
typedef struct {
    GLuint vbo, nbo, cbo, ibo;
    int nidx, cat, trim;
    uint32_t texkey, car_material;
    unsigned char draw_mode;  /* Optional override; zero uses the caller's mode. */
    unsigned char win;        /* car glass: N2Mesh.car_win (1..6 window, 0 none) */
} GpuMesh;

/* ---- static-world batching: meshes merged per (256m grid cell, texture) ----
 * One interleaved VBO per batch kills the per-mesh bind/attrib overhead;
 * batches stay small enough for the XY distance cull to keep working. */
typedef struct {
    float pos[3];
    float uv[2];
#ifdef __vita__
    signed char normal[4];    /* Vita: snorm8 xyz + pad (GL_BYTE, normalized):
                                 28 B vertices, 22% less RAM/VRAM than floats */
#else
    float normal[3];
#endif
    unsigned char col[4];     /* per-vertex RGBA prelight from the source stream
                                 (baked AO/lighting + terrain tint); 255=neutral */
} BatchedVertex;              /* 28 B on the Vita, 36 B elsewhere; interleaved */

#ifdef __vita__
static inline signed char bv_snorm8(float f) {
    float s = f * 127.0f; s += s < 0 ? -0.5f : 0.5f;
    return (signed char)(s > 127.0f ? 127 : s < -127.0f ? -127 : (int)s);
}
#define BV_SET_NORMAL(o, x, y, z) do { (o)->normal[0] = bv_snorm8(x);     (o)->normal[1] = bv_snorm8(y); (o)->normal[2] = bv_snorm8(z); (o)->normal[3] = 0; } while (0)
#define BV_NORMAL_GL_TYPE GL_BYTE
#define BV_NORMAL_GL_NORM GL_TRUE
#else
#define BV_SET_NORMAL(o, x, y, z) do { (o)->normal[0] = (x); (o)->normal[1] = (y);     (o)->normal[2] = (z); } while (0)
#define BV_NORMAL_GL_TYPE GL_FLOAT
#define BV_NORMAL_GL_NORM GL_FALSE
#endif

typedef struct {
    GLuint vbo;               /* unified interleaved VBO */
    GLuint ibo;               /* consolidated u16 IBO (<= 65535 verts/batch) */
    unsigned ioff;            /* byte offset of the indices in ibo (11-15: ibo == vbo,
                                 one buffer a batch, for worker-assembled batches) */
    int index_count;
    uint32_t texkey;          /* first member mesh's TPK key -- DIAGNOSTIC ONLY.
                             Do not gate drawing on it: a batch's members can
                             disagree about whether they named a texture, and
                             this reports one of them. Use `unresolved`. */
    unsigned char unresolved; /* 1 only when EVERY member named a TPK key and the
                             resolver produced nothing, i.e. the batch really is
                             missing art rather than being authored untextured.
                             A batch that mixes the two draws (flat colour) --
                             an untextured mesh must not vanish because a
                             neighbour in its cell lost its texture. */
    GLuint tex;               /* resolved GL texture (0 = untextured fallback) */
    int nmesh;                /* source meshes merged in (drawn-mesh metric) */
    int scen_count[8];        /* source N2_SC_* membership (visibility audit) */
    int emit_idx;             /* pre-sort emission index: upload_world_batches
                                 re-sorts by texture, so the draw-time array
                                 index is NOT the index batch_emit saw (M79) */
    float bbox_min[3];        /* culling bounds */
    float bbox_max[3];
    unsigned char drawmode;   /* N2_DRAW_* (M135): authored from the first
                                 member mesh's resolved texture record, via
                                 n2_tex_mode. N2_DRAW_OPAQUE (0) when no
                                 per-mesh mode table was supplied -- sky/glow/
                                 vista/diagnostic batches keep their own,
                                 separate, unrelated render treatment. */
    void *shared_buffers; /* owning GPU reference; render aliases merely borrow */
    unsigned char reflect;    /* members carry N2Mesh.reflect: the wet-road pass */
    unsigned char fade_in;    /* members carry N2Mesh.fade_in: newly arrived buildings */
    unsigned char tall;       /* members are tall far stand-ins (N2_BATCH_TALL): culled at the far
                                 plane, not the nearer world view distance */
} N2Batch;
#ifdef __vita__
/* Road reflection target (worldreflect.fx): the scene mirrored about the
   road plane, rendered small; reflective road batches add it on top. */
int    render_refl_begin(int w, int h, const float clear_rgb[3]);
void   render_refl_end(int screen_w, int screen_h);
GLuint render_refl_texture(void);
void render_prev_want(int on);   /* screen + env road reflection (11-15 prototype) */
int render_prev_valid(void);
int render_refl_screen(GpuMesh *quad, const float VP[16], float env_k);
/* the rear-view mirror's target (eView 3) and its composite (0x5cbf40) */
int    render_rvm_begin(int w, int h, const float clear_rgb[3]);
void   render_rvm_end(int screen_w, int screen_h);
GLuint render_rvm_texture(void);
void   render_rvm_composite(GpuMesh *quad, GLuint over, GLuint mask, int x, int y, int w, int h,
                             int screen_w, int screen_h);
/* debug: draw `tex` into the screen rectangle (x, y from bottom-left) */
void   render_debug_texture(GLuint tex, GpuMesh *quad, int x, int y, int w, int h,
                            int screen_w, int screen_h);
#define RENDER_REFL_UNIT 3   /* units 1-2 are the headlight shadow maps */
#define RENDER_ENV_UNIT 4    /* car environment atlas (car.fx) */
int    render_env_begin(int face, const float clear_rgb[3]);
void   render_env_end(int screen_w, int screen_h);
GLuint render_env_texture(void);
/* A fixed environment atlas (w x h RGBA, rows bottom first, the 3x2 face
   layout cubeuv() reads), e.g. the garage's FRONTEND/ENVMAPS cube; NULL
   frees it. render_env_use_static(1) makes render_env_texture return it. */
void   render_env_static(const unsigned char *rgba, int w, int h);
void   render_env_use_static(int on);
void   render_env_view(int face, const float eye[3], float *V);

#endif
void render_batch_release(N2Batch *batch);
void render_batch_reuse_selftest(void); /* GL context required on Vita */

/* the one shader program + its uniform handles */
typedef struct {
    GLuint prog;
    GLint uMVP, uModel, uUseTex, uColor, uUnlit, uAlpha, uSoft, uSpec, uAmbient, uDiffuse,
          uLight,   /* sun direction in world space */
          uVColor,  /* 0..1 strength of per-vertex prelight (world geometry only) */
          uDecal,   /* 1 = texture is an alpha-masked decal over uColor paint */
          uVista,                   /* >0.5: alpha-blended backdrop pass */
          uAlphaTest,               /* >0.5: discard texels below 0.5 alpha
                                       (N2_DRAW_CUTOUT world batches, M135) */
          uTextureAlpha,            /* >0.5: output alpha = texture alpha *
                                       uAlpha (N2_DRAW_BLEND/ADD world
                                       batches only, M135-R) */
          uEmissiveTex,             /* >0.5: texture-backed unlit RGB/alpha
                                       (authored sky and point-light sprites) */
          uFogColor, uFogDensity,   /* linear distance fog (world.fx FogValue) */
          uFogK, uFogFloor,         /* start*density, minimum fog factor */
          uCamPos,  /* camera in world space */
          uEnv,     /* environment-reflection amount (cars only) */
          uUVCheck, /* 1 = show the diagnostic UV-coordinate visualization
                       instead of lighting/texture (toggle lives in the
                       ImGui Session panel, make debug only) */
          uFlipN,   /* 1 = negate the vertex normal (inspector diagnostic) */
          uGloss,   /* specular pow() exponent: high = tight metallic-paint
                       highlight, low = broad plastic/trim sheen (cars only) */
          uFresnel, /* >0.5: alpha rides the fresnel term (car glass pass only) */
          uClearcoat, /* >0: second tight specular lobe -- the lacquer over the
                         coloured base coat (car body/trim only) */
          uRimTint, /* 0 = raw rim texture, 1 = recolor toward uColor (rim paint) */
          uEmissive; /* vec3 added AFTER lighting: a lamp lens that is lit AND
                        glowing. Going fully uUnlit instead is what made every
                        taillight a flat red sticker -- it discards the
                        specular, the fresnel rim and the environment
                        reflection, i.e. everything that reads as moulded
                        plastic. Dome-weighted by dot(N,V) in the shader, so a
                        curved lens is hottest where it faces the camera.
                        Zero everywhere else; the car pass clears it. */
    GLint uHeadPos, uHeadForward, uHeadRight, uHeadUp, uHeadShape, uHeadGain, uHeadLow, uHeadColor;
    GLint uHeadShadow, uHeadShadowMap[2], uHeadShadowMVP;
} RProg;

typedef struct {
    GLuint fbo, depth, texture[2], white, program;
    GLint mvp, cutout, range;
    int failed;
} HeadlightShadows;
/* Returns caster draws, or -1 if offscreen rendering is unavailable. */
int render_headlight_shadows(const RProg *r, HeadlightShadows *s, const N2Batch *batches, int count);
void free_headlight_shadows(HeadlightShadows *s);

void render_headlights(const RProg *r, const float model[16], const float anchors[4][4],
                       int high, float pitch, float range, float gain);

/* Select the material for a proven rear lens. Texture presence must not
 * disable emission. Brake lamps work independently of night running lights. */
void render_tail_lamp(const RProg *r,GLuint texture,int running,int braking,
                      int boost,float gain);

/* world-space sun direction (night scene key light) */
#define N2_SUN_X 0.4f
#define N2_SUN_Y 0.7f
#define N2_SUN_Z 0.6f

/* ---- tiny 4x4 matrix (column-major) ---- */
void mat_mul(const float *a, const float *b, float *o);
void mat_persp(float fov, float aspect, float znear, float zfar, float *m);
void mat_trans(float x, float y, float z, float *m);
void mat_rotz(float a, float *m);
void mat_car(const float *pos, float heading, const float *up, float rideh, float *m);
/* Unit-quad footprint centred on the model bounds, aligned to heading/ground.
 * pos is on the receiving ground, up is its unit normal. */
void mat_car_footprint(const float pos[3],float heading,const float up[3],
                       const float bb[6],float length_scale,float width_scale,
                       float lift,float m[16]);
void mat_lookat(const float *eye, const float *fwd, float *m);   /* up = world +Z */

/* ---- GPU objects ---- */
/* Shading transform only; uMVP still supplies clip-space placement. NULL
 * selects world geometry. Models must be rigid or uniformly scaled (mirrors
 * allowed), as supplied by mat_car and the wheel/brake hub transforms. */
void render_model(const RProg *r,const float model[16]);
RProg    render_program(void);          /* compile+link the shader, fetch uniforms */
#ifdef __vita__
void     render_world_lite_program(const RProg *main_prog);   /* Vita: cheap world FS */
#endif
GpuMesh *upload_scene(N2Scene *s);      /* VBO/NBO/IBO per mesh, normals computed */
/* same-state meshes marked in ok[] merged into one draw each (render.c) */
GpuMesh *upload_scene_merged(const N2Scene *s, const GpuMesh *gm, const unsigned char *ok,
                             short *lead, int *nout);
/* Re-upload one mesh's positions and normals through a row-vector matrix
   (an opening hood / door, partanim.c); NULL puts the mesh back as loaded. */
void render_mesh_repose(GpuMesh *g, const N2Mesh *m, const float *mat);
void free_scene_gpu(GpuMesh *gm, int count);
GpuMesh  make_wheel(float R, float halfW);  /* procedural tyre (see render.c) */
GLuint   make_wheel_tex(void);          /* radial alloy-rim texture for it */
GpuMesh  make_quad(void);               /* unit quad for HUD / billboards */
void     draw_gpumesh(GpuMesh *g);
/* Wheel material draw: paint is restricted to proven metal; rubber, backing
 * and plastic suppress metallic lighting. All touched uniforms, texture,
 * alpha, blend and depth state are restored. Missing textures stay opaque. */
void render_wheel_mesh(const RProg *r, GpuMesh *mesh, GLuint texture, int mode);
void render_wheel_mesh_n(const RProg *r, GpuMesh *mesh, GLuint texture, int mode,
                         int n, const float (*mvp)[16], const float (*world)[16], GLint uMVP);
/* Sort the four instances of a library's material slices by view depth.
 * order has space for 4 * scene->count entries (hub * count + slice). */
void render_wheel_order(const N2Scene *scene, const float mvp[4][16], int *order);
/* Caller provides scene->count indices; returns glass/cover count in draw order. */
int render_car_glass_order(const N2Scene *scene,const float mvp[16],int *order);

/* Caller binds r->prog on texture unit zero. Like draw_gpumesh, this sets mesh
 * attributes/buffers; pass uniforms, texture/blend/depth state are restored. */
/* halo: sprite size multiplier (1 = tuned default); gain: emission multiplier. */
/* Establish world transform/depth/additive state; restore ordinary depth writes
 * and fog on exit. Caller binds the program and texture unit zero. */
int render_world_glows(const RProg *r,const N2Batch *batches,int count,
                        const float mvp[16]);
int render_district_lights(const RProg *r, GpuMesh *quad, GLuint texture,
                           const N2LightSrc *lights, int nlights,
                           const float cam[3], const float look[3],
                           const float mvp[16], float viewdist,
                           float halo, float gain);

/* Merge the static world into per-(cell,texture) batches and upload them.
 * mtex = per-mesh resolved GL texture, texTerr = grass fallback for terrain
 * meshes without one. Sorted by texture so binds are rare. The CPU-side scene
 * is left untouched (physics reads it). Returns the batch count. */
/* `audit` (Milestone 75): when non-NULL, the batch that swallows the source
 * mesh of that sname is additionally verified vertex-by-vertex and reported on
 * stdout. Purely additive — the GL upload is identical either way, so the audit
 * observes the production path rather than a parallel reimplementation. */
/* meshbatch (optional, NULL to skip): caller-sized s->count array receiving the
 * FINAL batch index each source mesh ended in, or -1 if it never entered the
 * world partition (sky/glow go to their own passes). Written after the texture
 * re-sort, so the indices are the ones the renderer uses (M133). */
/* mtexmode (optional, NULL to skip): per-mesh N2_DRAW_* (M135), mirroring
 * mtex -- the authored draw mode of the mesh's own resolved texture record.
 * Every mesh a batch merges shares one texture, hence one mode; batch_emit
 * takes it from the first member. NULL leaves every batch N2_DRAW_OPAQUE,
 * unchanged from before this field existed. */
int  upload_world_batches(const N2Scene *s, const float (*mbb)[4],
                          const GLuint *mtex, GLuint texTerr, N2Batch **out,
                          const char *audit, int *meshbatch,
                          const unsigned char *mtexmode);
/* Same partition, resumed on the GL thread. Scene and mode arrays are borrowed
 * and must stay immutable until completion/cancellation. Step returns 0 while
 * pending, 1 after transferring complete output, or -1 on failure. Pending and
 * failed steps leave outputs untouched. A quota is not a hard time deadline. */
typedef struct WorldBatchUpload WorldBatchUpload;
WorldBatchUpload *upload_world_batches_begin(const N2Scene *s,
                          const float (*mbb)[4], const GLuint *mtex,
                          GLuint texTerr, const char *audit,
                          const unsigned char *mtexmode);
int upload_world_batches_step(WorldBatchUpload **job, int max_batches,
                          N2Batch **out, int *count, int *meshbatch);
void upload_world_batches_cancel(WorldBatchUpload **job);
/* Optional per-mesh normals (scene order) computed ahead of time, e.g. on a
   worker thread; NULL entries fall back to computing them during upload. */
void upload_world_batches_set_normals(WorldBatchUpload *job, float *const *normals);
#ifdef __vita__
/* Scaled 3D rendering (PS Vita): the scene renders into an offscreen colour
   texture + depth at a fraction of the screen, then is stretched over the
   full framebuffer before the HUD, which stays native resolution. The GPU is
   fill-rate bound, so pixels saved are frame time saved. */
int  render_scene_begin(int screen_w, int screen_h, float scale);   /* 1 = scaled */
void render_scene_end(const RProg *rp, GpuMesh *quad, int screen_w, int screen_h,
                      float fog_density);
/* 1 while the 3D frame renders into the scene texture: its framebuffer,
   colour texture and size (for GPU-side copies of the frame) */
int  render_scene_target(unsigned *fbo, unsigned *tex, int *w, int *h);
/* the screen flash: the next composites multiply the 3D frame by mul (rgb)
   before the visual treatment; NULL = none */
void render_set_flash(const float mul[3]);
/* the speed / nitrous motion blur (0x5cc460) for the next composites:
   strength k (0 = off) and the first pass's corner offsets o (left x, top y,
   right x, bottom y, in texture units); and the bloom level f (0x5bc180) */
void render_set_speedblur(float k, const float o[4]);
void render_set_bloom_level(float f);
/* the scene camera's depth of field for the next composites (camera +0xb4
   centre / +0xb8 range in metres, 0 0 = off; 0x5d2850) and its letterbox
   (camera +0xc8, 0 = none; 0x5cbdc0) */
void render_set_dof(float centre, float range);
void render_set_letterbox(float lb);
/* around the main 3D pass: opaque draws write the depth of field depth to
   the frame's alpha while on (Vita; a no-op without a depth of field) */
void render_dof_scene(int on);
int  render_dof_writes(void);   /* 1 while that alpha is being written */
#endif
/* The flat per-vertex normals batch upload uses (nverts*3 floats). */
void render_mesh_normals(const N2Mesh *m, float *nor);
/* A mesh already in batch vertex layout (normals included) plus its 3D
   bounds, prepared off the render thread so batch upload only copies. */
typedef struct { BatchedVertex *v; float bb[6]; unsigned char borrowed, hash_valid; uint64_t hash[2]; } PackedMesh;
int  render_pack_mesh(const N2Mesh *m, PackedMesh *out);    /* 1 = ok */
/* Vita: drop CPU batch copies whose GPU buffers now exist (keeps bb/hash) */
void render_world_packed_trim(const N2Scene *s);
/* The same over only the listed meshes (the worker's list of meshes whose
 * shared geometry holds a packed copy); list NULL walks the whole scene. */
void render_world_packed_trim_list(const N2Scene *s, const int *list, int n);
void render_world_packed_trim_flush(void);
/* Vita worker: pack the meshes of planned batches the GPU does not hold (see render.c) */
int render_world_plan_fill(WorldBatchUpload *j);
/* Apply upload_tex's 64px+ box halving ahead of time (any thread). */
void render_prehalve_tex(N2Tex *t);
extern int g_world_dxt;
int render_world_dxt_enabled(void);
/* Safe single-level upload plan; rejects unsupported dimensions/short blobs. */
int render_dxt_base_plan(const N2Tex *t, int *w, int *h, int *offset, int *bytes);
void render_dxt_plan_selftest(void);
/* Borrows the array; consumes and clears each vertex buffer after copying it.
   Cancellation leaves unconsumed entries owned by the caller. */
void upload_world_batches_set_packed(WorldBatchUpload *job, PackedMesh *packed);
WorldBatchUpload *upload_world_batches_prepare_cpu(const N2Scene *scene,
    const float (*bounds)[4], const GLuint *logical_textures,
    const unsigned char *modes, PackedMesh *packed);
int upload_world_batches_bind_prepared(WorldBatchUpload *job, const GLuint *textures,
    GLuint terrain_texture, const unsigned char *modes);
/* The same, when the caller already matched every mesh's texture and mode
 * with the worker's plan: skips the per-mesh revalidation. */
int upload_world_batches_bind_checked(WorldBatchUpload *job, const GLuint *textures,
    GLuint terrain_texture, const unsigned char *modes);
/* Same merge, but for one category (N2_SKY / N2_GLOW) pulled out of the main
 * batching pass above — grouped by texture only, no spatial cell/cull grid,
 * since there are only ever a handful of skybox/neon meshes per city. Sky,
 * glow, vista and diagnostic-marker batches keep their own dedicated render
 * treatment, so their callers pass mtexmode = NULL (-> N2_DRAW_OPAQUE). */
extern int g_glow_cells;   /* Beyond: glow batches per world cell (render.c) */
int  upload_cat_batches(const N2Scene *s, int cat, const GLuint *mtex, N2Batch **out,
                        const unsigned char *mtexmode);
void render_batch_array_free(N2Batch **batches, int *count);
void draw_batch(const N2Batch *b);
GLuint   upload_tex(const N2Tex *t);
/* Upload a decoded TPK texture, preferring a direct glCompressedTexImage2D of
   its raw S3TC blocks when the driver supports the format; falls back to the
   portable CPU-decoded RGBA path (upload_tex) otherwise. Leaves the texture
   bound (callers may override wrap/filter after). */
GLuint   upload_tpk_texture_to_gpu(const N2Tex *t);
/* Anisotropic filtering, the one texture-detail knob this renderer was missing.
 * Trilinear alone samples a square footprint, so any surface seen at a grazing
 * angle -- which is most of a road, a kerb and a car's own flanks -- drops to a
 * far coarser mip than it needs and smears. Measured on a race frame: 6.2% of
 * pixels change between 1x and 16x. `render_texture_detail` re-applies the
 * setting to every texture already uploaded, so the in-game slider takes effect
 * immediately instead of on the next load. */
extern int   g_tex_aniso_max;   /* 1 = extension absent; else the GL maximum */
extern float g_tex_aniso;       /* current setting, clamped to [1, max] */
void render_texture_detail(float aniso);
extern int g_tex_s3tc;   /* 1 = GL_EXT_texture_compression_s3tc present; set once at GL init */

/* ---- 3x5 bitmap font (uppercase, digits, _ - /) ---- */
void  draw_text(GpuMesh *quad, GLint uMVP, const char *s,
                float x, float y, float px, float py);
float text_width(const char *s, float px);

/* dev screenshot: rgb is top-left origin, w*h*3 */
void write_png(const char *path, int w, int h, const unsigned char *rgb);

/* car.fx (oldcar.txt = the port's earlier car shader): begin binds the env
   atlas and selects the car variant, part sets the light material of the
   next draw (body/misc without one = paint), end goes back. */
int    render_carfx_enabled(void);
int    render_carfx_active(void);
void   render_carfx_begin(const RProg *r);
void   render_carfx_sh(const float sh[10][4]);
void   render_carfx_part(uint32_t material, int cat);
/* 0x48d860 (ReplaceLightMaterial) as the car renderer uses it: while set,
   meshes with one of these light materials wear the paint material
   (carfx_use_paint) -- painted calipers, exhaust tips and rims. n 0 clears. */
void   render_carfx_swap(const uint32_t *materials, int n);
int    render_carfx_swapped(uint32_t material);
void   render_carfx_end(void);

/* worldbone.fx (SPEED2.EXE IDI_WORLDBONE_FX): the pre-race scene's skinned
   characters. Positions/normals come skinned into the world (nisdraw.c);
   colour = 2 * tex * saturate(SH(N) * (DiffuseMin + DiffuseRange * N.E)).
   Uniforms: uMVP, uEye, uSH0..9, uDMin, uDRange, uTex, uCut, uBlend, uDofW. */
GLuint render_bone_program(void);

/* worldnodepth.fx: the Acid particles (aciddraw.c). Attributes aPos 0,
   aUV 1, aColor 3 (normalised bytes); uniforms uMVP, uTex. */
/* the particle program's colour attribute (aciddraw.c) */
#ifdef __vita__
#define RENDER_PARTICLE_COLOR_LOC 2
#else
#define RENDER_PARTICLE_COLOR_LOC 3
#endif
GLuint render_particle_program(void);

#endif
