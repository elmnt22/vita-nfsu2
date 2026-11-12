/* rain.c -- the original game's rain, ported from SPEED2.EXE (names from the
 * GameCube Speed.elf): Rain::Init 0x613050, SetIntensity 0x6139b0, Change
 * 0x613a30, Wind 0x6136b0, Update 0x61e700, Render 0x613a70 (the batched
 * path 0x5bb430, [0x803abc] = 2), UpdateAndRender 0x61f120, IsRainingAt
 * 0x6134e0, GetDesiredRainIntensity 0x61e380; OnScreenRain (ctor 0x614090,
 * Update 0x6140f0, drawn by 0x5c7ab0 through rain_drop.fx); the rain's part
 * of FogQuery::CalculateFogInfo 0x614300 and of the screen effect pass
 * 0x5c7860 (ScreenEffectDB slot 2).
 *
 * Costs for the Vita perf pass: at most intensity x 400 streak quads (one
 * draw, one texture, depth test, no depth write, small on screen) and, while
 * it rains in the open, up to 20 screen drops (one draw) plus one full-frame
 * copy of the scene a frame. On the Vita the copy is a GPU pass from the
 * scene texture (scene_copy_gpu): glCopyTexSubImage2D is a GPU sync + CPU
 * readback in vitaGL (~50 ms a frame on HW 10-31). RAIN_VITA_CAP below is the
 * Vita-only switch for both; it is off (0): retail counts.
 */
#include "rain.h"
#include "render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Vita-only budget switch: 0 = retail (400 streaks, 20 screen drops, the
   scene copy every frame). Set it to cap the streaks at RAIN_VITA_CAP and
   halve the screen drops. Off by default. */
#ifndef RAIN_VITA_CAP
#define RAIN_VITA_CAP 0
#endif

#ifdef N2_GLES
#  define RGLSL_FS "precision mediump float;\n"
#  define RGLSL_VS "precision highp float;\n"
#else
#  define RGLSL_FS "#version 120\n#define lowp\n#define mediump\n#define highp\n"
#  define RGLSL_VS RGLSL_FS
#endif

#define NDROPS 400
#define NSCREEN 20
#define PI2 6.28318530718f

/* ---- the track's parameter tables (chunk 0x8003B600) ---- */
typedef struct {
    int ok, u8tree;
    float box[4];
    int nodes, entries, stride, values;
    unsigned char *pal, *tree;
} RTab;
typedef struct { float cur[8], last[2]; int have; } RBlend;   /* ParameterAccessorBlendByDistance */

typedef struct {
    float pos[2][3];          /* the two position buffers (+0x1f8 / +0x1fc pick) */
    int wind;                 /* +0x20: one of the ten wind entries */
    unsigned char state, type, sub, front;   /* +0x24 .. +0x27 */
} RDrop;

typedef struct { float x, y, life, maxlife, size, speed; } RScreenDrop;

static struct {
    int ready;
    GLuint prog, vbo, ibo, tex; int blend; GLint uMVP, uTex;
    GLint uFogColor, uFogDensity, uFogK, uFogFloor;   /* streaks: world.fx FogValue */
    float fog_col[3], fog_den, fog_k, fog_floor;
    GLuint sprog, stex, scene; int scene_w, scene_h; GLint sOff, sAlpha, sDrop, sScene;
    /* the GPU copy of the scene (Vita perf): a texture of the scene FBO's
       size drawn from the scene texture, instead of glCopyTexSubImage2D */
    GLuint cprog, cfbo, ctex; int cw, ch, cfailed;
    RTab rain, wind, fog, fogdist;
    RTab amb, vols;   /* "Ambience" / "Volumes": EAXS_PFController's AmbientAccessor / VolumeAccessor */
    RBlend wind_b, fog_b;
    float clock;                       /* the world clock 0x7fb718 (seconds) */
    float I, desired, wet, wet_target; /* +0x1f0, +0x5094, +0x509c, +0x5098 */
    RDrop d[NDROPS];
    int cur;                           /* the buffer written this frame */
    unsigned count[6], want[6];        /* +0x4d04 counts, +0x4d1c desired */
    float frac[6];                     /* +0x4d34 */
    unsigned active;                   /* +0x4d00 drops in use: 400 */
    float windv[10][3];                /* +0x4da0.. per-drop wind */
    float size[12];                    /* +0x4d4c [type*2 + sub] */
    float side[4], streak[4]; int velflag[4];
    int gust_state; float gust[3], gust_tgt[3], gust_period, gust_timer, gust_lerp;
    float wvec[3];                     /* +0x5100 */
    float R[9];                        /* the viewing car's rows (fInvertMatrix 0x5050: its inverse) */
    float camv[3], camspeed;           /* rain-space camera step, +0x5090 */
    float carpos[3], ca, sa;           /* the rain space 0x5010: RotZ(heading) + the car */
    unsigned short last_heading; int have_heading;
    float last_eye[3]; int have_eye; float eye_vel[3];
    float camdir[3];
    int cover_car, cover_ahead;        /* +0x1e4, +0x1e8 */
    RScreenDrop sd[NSCREEN]; int nscreen;
    int fogc[3];                       /* +0x4edc .. +0x4ee4 */
    unsigned rng;
    float force;                       /* OPENUG2_RAIN_FORCE (test aid), < 0 off */
    int log; float logt;
    int was_raining;
} R;

static float rnd(float r)   /* bRandom(float) 0x43c1f0: [0, r) */
{
    if (!R.rng) R.rng = 0x1234567u;
    R.rng ^= R.rng << 13; R.rng ^= R.rng >> 17; R.rng ^= R.rng << 5;
    return (float)(R.rng >> 8) * (1.0f / 16777216.0f) * r;
}
static int ftol(float f) { return (int)f; }
static float bsin_u(int a) { return sinf((float)(a & 0xffff) * (PI2 / 65536.0f)); }
static float bcos_u(int a) { return cosf((float)(a & 0xffff) * (PI2 / 65536.0f)); }
static int deg_units(float deg) { return ftol(deg * 65536.0f) / 360; }   /* 0xb60b60b7 magic */
static void vnorm(float v[3])
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static void vcross(float o[3], const float a[3], const float b[3])   /* bCross 0x43d1e0 */
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
/* world vector -> the car's axes (the inverse of its rotation, translation 0) */
static void to_car(float o[3], const float w[3])
{
    float x = w[0] * R.R[0] + w[1] * R.R[1] + w[2] * R.R[2];
    float y = w[0] * R.R[3] + w[1] * R.R[4] + w[2] * R.R[5];
    float z = w[0] * R.R[6] + w[1] * R.R[7] + w[2] * R.R[8];
    o[0] = x; o[1] = y; o[2] = z;
}

/* ---- tables ---- */
static uint32_t rhash(const char *s)
{
    uint32_t h = 0xFFFFFFFFu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}
static void tab_free(RTab *t) { free(t->pal); free(t->tree); memset(t, 0, sizeof *t); }
static void tab_parse(const unsigned char *blk, uint32_t blen, const char *name, RTab *t)
{
    uint32_t want = rhash(name);
    for (uint32_t o = 0; o + 8 <= blen; ) {
        uint32_t m, s; memcpy(&m, blk + o, 4); memcpy(&s, blk + o + 4, 4);
        if (o + 8 + s > blen) break;
        if (m == 0x8003B601u && s >= 44) {
            const unsigned char *q = blk + o + 8, *qe = q + s;
            uint32_t hash; memcpy(&hash, q + 8, 4);
            if (hash == want) {
                const unsigned char *hdr = NULL, *pal = NULL, *tree = NULL;
                uint32_t pal_len = 0, tree_len = 0; int u8 = 0;
                while (q + 8 <= qe) {
                    uint32_t cm, cs; memcpy(&cm, q, 4); memcpy(&cs, q + 4, 4);
                    if (q + 8 + cs > qe) break;
                    if (cm == 0x3B602u && cs >= 36) hdr = q + 8;
                    else if (cm == 0x3B605u) { pal = q + 8; pal_len = cs; }
                    else if (cm == 0x3B608u) { tree = q + 8; tree_len = cs; u8 = 0; }
                    else if (cm == 0x3B607u) { tree = q + 8; tree_len = cs; u8 = 1; }
                    q += 8 + cs;
                }
                if (hdr && pal) {
                    uint32_t v[4]; memcpy(t->box, hdr + 4, 16); memcpy(v, hdr + 20, 16);
                    t->nodes = (int)v[0]; t->entries = (int)v[1]; t->stride = (int)v[2]; t->values = (int)v[3];
                    t->u8tree = u8;
                    uint32_t need_tree = (uint32_t)t->nodes * (u8 ? 4u : 8u);
                    if (t->entries > 0 && t->values > 0 && t->values <= 8 && t->stride >= t->values * 4 &&
                        pal_len >= (uint32_t)(t->entries * t->stride) && (t->nodes == 0 || (tree && tree_len >= need_tree))) {
                        t->pal = (unsigned char *)malloc(pal_len);
                        t->tree = (unsigned char *)malloc(need_tree ? need_tree : 1);
                        if (t->pal && t->tree) {
                            memcpy(t->pal, pal, pal_len);
                            if (need_tree) memcpy(t->tree, tree, need_tree);
                            t->ok = 1;
                        }
                    }
                }
                return;
            }
        }
        o += 8 + s;
    }
}
/* ParameterAccessor: the quadtree's leaf at (x, y). Outside the box too:
   GetParameterSetIndexFromQuadData8 / 16 0x5f0930 / 0x5f0a30 (via
   GetParameterData 0x5fa4e0) never take their "outside -> 0" exit (it needs
   x < xmin && x > xmax ...), so the walk lands on the edge leaf (R1 11-01) */
static const float *tab_entry(const RTab *t, float x, float y)
{
    float x0 = t->box[0], y0 = t->box[1], x1 = t->box[2], y1 = t->box[3];
    int idx = 0;
    if (t->nodes > 0) {
        int n = 0;
#define TW(n, k) (t->u8tree ? (int)t->tree[(n) * 4 + (k)] \
                            : (int)(t->tree[((n) * 4 + (k)) * 2] | t->tree[((n) * 4 + (k)) * 2 + 1] << 8))
        for (int guard = 0; guard < 64 && TW(n, 0) != 0; guard++) {
            float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
            int c = x < mx ? (y < my ? 0 : 1) : (y < my ? 2 : 3);
            if (x < mx) x1 = mx; else x0 = mx;
            if (y < my) y1 = my; else y0 = my;
            n = TW(n, c);
            if (n >= t->nodes) { n = 0; break; }
        }
        idx = TW(n, 0) == 0 ? TW(n, 1) : 0;
#undef TW
    }
    if (idx >= t->entries) idx = 0;
    return (const float *)(t->pal + (size_t)idx * (size_t)t->stride);
}
/* ParameterAccessorBlendByDistance::CaptureData 0x5fa660 */
static const float *tab_blend(const RTab *t, RBlend *b, float x, float y, float dist)
{
    const float *e = tab_entry(t, x, y);
    float k = 1.0f;
    if (b->have) {
        float dx = x - b->last[0], dy = y - b->last[1], dd = sqrtf(dx * dx + dy * dy);
        k = dd < dist ? dd / dist : 1.0f;
    }
    for (int i = 0; i < t->values && i < 8; i++) b->cur[i] = b->have ? b->cur[i] + (e[i] - b->cur[i]) * k : e[i];
    b->have = 1; b->last[0] = x; b->last[1] = y;
    return b->cur;
}

/* ---- Rain::Init(type, 1.0) 0x613050 and SetRainBase 0x61e0f0 ---- */
static void rain_reset(void)
{
    static const float sizes[12] = { 0.3f, 0.15f, 1.0f, 0.5f, 0.4f, 0.2f, 0.3f, 0.15f, 0, 0, 1, 1 };
    static const float side[4] = { 0.03f, 0.1f, 0.05f, 0.05f }, streak[4] = { 0.7f, 0.1f, 0.05f, 0.05f };
    static const int velflag[4] = { 1, 0, 1, 0 };
    memcpy(R.size, sizes, sizeof sizes); memcpy(R.side, side, sizeof side);
    memcpy(R.streak, streak, sizeof streak); memcpy(R.velflag, velflag, sizeof velflag);
    R.active = NDROPS;
    memset(R.count, 0, sizeof R.count); memset(R.want, 0, sizeof R.want); memset(R.frac, 0, sizeof R.frac);
    for (int i = 0; i < NDROPS; i++) {
        RDrop *p = &R.d[i];
        p->state = 0; p->type = 5; p->sub = (unsigned char)ftol(rnd(2.0f)); p->front = 0;
        p->pos[0][0] = rnd(20.0f) - 10.0f + 8.0f;
        p->pos[0][1] = rnd(7.0f) - 3.5f;
        p->pos[0][2] = rnd(6.0f);
        memcpy(p->pos[1], p->pos[0], sizeof p->pos[0]);
        p->wind = i % 10;
    }
    R.count[5] = NDROPS;
    for (int k = 0; k < 10; k++) {
        R.windv[k][0] = rnd(0.02f) - 0.01f;
        R.windv[k][1] = rnd(0.02f) - 0.01f;
        R.windv[k][2] = -rnd(0.03f) - 0.45f;
    }
    R.gust_state = 2;
    memset(R.gust, 0, sizeof R.gust); memset(R.gust_tgt, 0, sizeof R.gust_tgt);
    R.gust_period = R.gust_timer = R.gust_lerp = 0;
    R.cur = 0; R.have_heading = 0; R.have_eye = 0;
    R.I = 0; R.desired = 0; R.wet = 1.0f;   /* SetRainBase: +0x509c = 1 */
    R.nscreen = 0;
    for (int i = 0; i < NSCREEN; i++) {   /* OnScreenRain::OnScreenRain 0x614090 */
        RScreenDrop *s = &R.sd[i];
        s->x = s->y = s->life = 0;
        s->maxlife = rnd(1.5f) + 0.3f;
        s->speed = rnd(0.1f) + 0.02f;
        s->size = rnd(1.0f);
    }
}

/* ---- GL ---- */
static GLuint rcompile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); printf("rain: shader: %s\n", log); }
    return s;
}
/* streaks: colour = saturate(2 * tex * vertex colour), alpha = tex * vertex
   alpha, then the world's fog: fog = clamp(1 - w*density + k, floor, 1) */
static const char *VS_STREAK =
    RGLSL_VS
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec4 aColor;\n"
    "uniform mat4 uMVP;\n"
    "varying vec2 vUV; varying vec4 vC; varying float vDepth;\n"
    "void main(){ vUV=aUV; vC=aColor; gl_Position=uMVP*vec4(aPos,1.0); vDepth=gl_Position.w; }\n";
static const char *FS_STREAK =
    RGLSL_FS
    "varying vec2 vUV; varying vec4 vC; varying float vDepth;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec3 uFogColor; uniform float uFogDensity, uFogK, uFogFloor;\n"
    "void main(){ vec4 t=texture2D(uTex,vUV);\n"
    "  float fog=clamp(1.0-vDepth*uFogDensity+uFogK,uFogFloor,1.0);\n"
    "  gl_FragColor=vec4(mix(uFogColor,clamp(2.0*t.rgb*vC.rgb,0.0,1.0),fog),t.a*vC.a); }\n";
/* rain_drop.fx: the drop texture's G / B bend the scene sample, its alpha
   times the drop's fade is the cover; the scene is brightened by 1.2 */
static const char *VS_DROP =
    RGLSL_VS
    "attribute vec2 aPos; attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }\n";
static const char *FS_DROP =
    RGLSL_FS
    "varying vec2 vUV;\n"
    "uniform sampler2D uDrop; uniform sampler2D uScene;\n"
    "uniform vec4 uOff; uniform float uAlpha;\n"
    "void main(){ vec4 d=texture2D(uDrop,vUV);\n"
    "  vec2 s=vec2(d.g*uOff.z+uOff.x, d.b*uOff.w+uOff.y);\n"
    "  vec3 c=texture2D(uScene,vec2(s.x,1.0-s.y)).rgb*1.2;\n"
    "  gl_FragColor=vec4(c,d.a*uAlpha); }\n";
/* the scene copy (port, Vita perf): a straight texel copy, same size */
static const char *FS_COPY =
    RGLSL_FS
    "varying vec2 vUV;\n"   /* as the bloom composite's full-frame reads */
    "uniform sampler2D uScene;\n"
    "void main(){ gl_FragColor=vec4(texture2D(uScene,vUV).rgb,1.0); }\n";

static GLuint tex_from(const unsigned char *b, long len, N2Tpk *tk, uint32_t key, int *blend)
{
    N2Tex t; memset(&t, 0, sizeof t);
    GLuint id = 0;
    if (n2_tpk_decode(b, len, *tk, key, &t) && t.rgb) { id = upload_tex(&t); if (blend) *blend = t.blend; }
    free(t.rgb); free(t.alpha); free(t.dxt);
    if (id) return id;
    /* RAINDROPPC is format tag 0x20 (uncompressed B8G8R8A8), which the shared
       decoder skips: read the record here (same +0x18 hash / +0x24 offset /
       +0x38 size / +0x3e format layout n2_tpk_decode uses), top mip only */
    for (int k = 0; k < tk->nblk && !id; k++) {
        long hb = tk->blk[k].hbeg, he = hb + tk->blk[k].hsize, db = tk->blk[k].dbase;
        if (db < 0) continue;
        for (long i = hb; i + 0x40 < he; i++) {
            if (n2_u32(b + i + 0x18) != key || b[i + 0x3e] != 0x20) continue;
            uint32_t off = n2_u32(b + i + 0x24);
            int w = b[i+0x38] | b[i+0x39] << 8, h = b[i+0x3a] | b[i+0x3b] << 8;
            if (w <= 0 || h <= 0 || w > 256 || h > 256 || db + (long)off + (long)w*h*4 > len) continue;
            const unsigned char *p = b + db + off;
            unsigned char *rgba = (unsigned char *)malloc((size_t)w * h * 4);
            for (long q = 0; q < (long)w*h; q++) {
                rgba[q*4] = p[q*4+2]; rgba[q*4+1] = p[q*4+1]; rgba[q*4+2] = p[q*4]; rgba[q*4+3] = p[q*4+3];
            }
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glBindTexture(GL_TEXTURE_2D, 0);
            free(rgba);
            break;
        }
    }
    return id;
}

static float g_clock0 = 1.0f;   /* ResetWorldTime: 1 s */

int rain_init(const unsigned char *glob, long globlen)
{
    const char *e = getenv("OPENUG2_RAIN_FORCE");
    R.force = e && *e ? (float)atof(e) : -1.0f;
    e = getenv("OPENUG2_RAIN_LOG");
    R.log = e && *e && *e != '0';
    e = getenv("OPENUG2_RAIN_CLOCK");   /* test aid: the world clock a new World starts at */
    if (e && *e) g_clock0 = (float)atof(e);
    if (!R.active) { R.clock = g_clock0; rain_reset(); }
    if (R.ready) return R.tex != 0;
    R.ready = 1;
    /* the streaks: the particle colour equation plus the world's linear
       distance fog (the batched quads go through world.fx, which fogs) */
    R.prog = glCreateProgram();
    glAttachShader(R.prog, rcompile(GL_VERTEX_SHADER, VS_STREAK));
    glAttachShader(R.prog, rcompile(GL_FRAGMENT_SHADER, FS_STREAK));
    glBindAttribLocation(R.prog, 0, "aPos");
    glBindAttribLocation(R.prog, 1, "aUV");
    glBindAttribLocation(R.prog, RENDER_PARTICLE_COLOR_LOC, "aColor");
    glLinkProgram(R.prog);
    {
        GLint ok = 0; glGetProgramiv(R.prog, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(R.prog); R.prog = render_particle_program(); }
    }
    if (R.prog) {
        R.uMVP = glGetUniformLocation(R.prog, "uMVP");
        R.uTex = glGetUniformLocation(R.prog, "uTex");
        R.uFogColor = glGetUniformLocation(R.prog, "uFogColor");
        R.uFogDensity = glGetUniformLocation(R.prog, "uFogDensity");
        R.uFogK = glGetUniformLocation(R.prog, "uFogK");
        R.uFogFloor = glGetUniformLocation(R.prog, "uFogFloor");
    }
    glGenBuffers(1, &R.vbo);
    glGenBuffers(1, &R.ibo);
    {
        static unsigned short ix[NDROPS * 6];
        for (int q = 0; q < NDROPS; q++) {
            unsigned short b = (unsigned short)(q * 4);
            unsigned short *o = ix + q * 6;
            o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, R.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof ix, ix, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    R.sprog = glCreateProgram();
    glAttachShader(R.sprog, rcompile(GL_VERTEX_SHADER, VS_DROP));
    glAttachShader(R.sprog, rcompile(GL_FRAGMENT_SHADER, FS_DROP));
    glBindAttribLocation(R.sprog, 0, "aPos");
    glBindAttribLocation(R.sprog, 1, "aUV");
    glLinkProgram(R.sprog);
    GLint ok = 0; glGetProgramiv(R.sprog, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(R.sprog); R.sprog = 0; }
    else {
        R.sOff = glGetUniformLocation(R.sprog, "uOff");
        R.sAlpha = glGetUniformLocation(R.sprog, "uAlpha");
        R.sDrop = glGetUniformLocation(R.sprog, "uDrop");
        R.sScene = glGetUniformLocation(R.sprog, "uScene");
    }
#ifdef __vita__
    if (R.sprog) {   /* the GPU scene copy (rain_draw_screen) */
        R.cprog = glCreateProgram();
        glAttachShader(R.cprog, rcompile(GL_VERTEX_SHADER, VS_DROP));
        glAttachShader(R.cprog, rcompile(GL_FRAGMENT_SHADER, FS_COPY));
        glBindAttribLocation(R.cprog, 0, "aPos");
        glBindAttribLocation(R.cprog, 1, "aUV");
        glLinkProgram(R.cprog);
        ok = 0; glGetProgramiv(R.cprog, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(R.cprog); R.cprog = 0; }
    }
#endif
    /* GLOBALB holds two texture packs (top-level 0xB3300000): RAINDROP is
       in the first, RAINDROPPC in the second */
    for (long o = 0; glob && o + 8 <= globlen && (!R.tex || !R.stex); ) {
        uint32_t m, s; memcpy(&m, glob + o, 4); memcpy(&s, glob + o + 4, 4);
        if ((long)s > globlen - o - 8) break;
        if (m == 0xB3300000u) {
            const unsigned char *b = glob + o; long len = (long)s + 8;
            N2Tpk tk = n2_tpk_open(b, len);
            if (!R.tex) R.tex = tex_from(b, len, &tk, 0x7415ea5eu, &R.blend);   /* RAINDROP */
            if (!R.stex) R.stex = tex_from(b, len, &tk, 0xd13a0471u, NULL);     /* RAINDROPPC */
            free(tk.blk);
        }
        o += 8 + (long)s;
    }
    if (glob && (!R.tex || !R.stex)) {   /* not a plain chunk list: the whole buffer */
        N2Tpk tk = n2_tpk_open(glob, globlen);
        if (!R.tex) R.tex = tex_from(glob, globlen, &tk, 0x7415ea5eu, &R.blend);
        if (!R.stex) R.stex = tex_from(glob, globlen, &tk, 0xd13a0471u, NULL);
        free(tk.blk);
    }
    printf("rain: RAINDROP %s (blend %d), RAINDROPPC %s, drop program %s%s\n",
           R.tex ? "ok" : "missing", R.blend, R.stex ? "ok" : "missing", R.sprog ? "ok" : "failed",
           R.force >= 0 ? " (OPENUG2_RAIN_FORCE)" : "");
    return R.tex != 0;
}

void rain_free(void)
{
    if (!R.ready) return;
    if (R.tex) glDeleteTextures(1, &R.tex);
    if (R.stex) glDeleteTextures(1, &R.stex);
    if (R.scene) glDeleteTextures(1, &R.scene);
#ifdef __vita__
    if (R.cfbo) glDeleteFramebuffers(1, &R.cfbo);
    if (R.ctex) glDeleteTextures(1, &R.ctex);
    if (R.cprog) glDeleteProgram(R.cprog);
#endif
    if (R.vbo) glDeleteBuffers(1, &R.vbo);
    if (R.ibo) glDeleteBuffers(1, &R.ibo);
    if (R.sprog) glDeleteProgram(R.sprog);
    tab_free(&R.rain); tab_free(&R.wind); tab_free(&R.fog); tab_free(&R.fogdist);
    tab_free(&R.amb); tab_free(&R.vols);
    memset(&R, 0, sizeof R);
}

/* World::World runs ResetWorldTime (0x576cf0) and builds a new Rain for every
   race and every free-roam session, not only on a track change */
void rain_set_world_fog(const float col[3], float density, float k, float floor_)
{
    memcpy(R.fog_col, col, sizeof R.fog_col);
    R.fog_den = density; R.fog_k = k; R.fog_floor = floor_;
}

void rain_world_reset(void)
{
    R.clock = g_clock0;
    rain_reset();
    R.was_raining = 0;
}

int rain_load_track(const char *troot, const char *track)
{
    tab_free(&R.rain); tab_free(&R.wind); tab_free(&R.fog); tab_free(&R.fogdist);
    tab_free(&R.amb); tab_free(&R.vols);
    memset(&R.wind_b, 0, sizeof R.wind_b); memset(&R.fog_b, 0, sizeof R.fog_b);
    R.clock = g_clock0;   /* ResetWorldTime 0x576cf0 from World::World */
    rain_reset();
    memset(R.fogc, 0, sizeof R.fogc);
    R.was_raining = 0;
    if (!troot || !track) return 0;
    const char *stem = !strncmp(track, "STREAM", 6) ? track + 6 : track;
    char path[1024]; snprintf(path, sizeof path, "%s/%s.BUN", troot, stem);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t hd[2]; unsigned char *blk = NULL; uint32_t blen = 0;
    while (fread(hd, 4, 2, f) == 2) {
        if (hd[0] == 0x8003B600u) {
            blk = (unsigned char *)malloc(hd[1] ? hd[1] : 1); blen = hd[1];
            if (!blk || fread(blk, 1, blen, f) != blen) { free(blk); blk = NULL; }
            break;
        }
        if (fseek(f, (long)hd[1], SEEK_CUR)) break;
    }
    fclose(f);
    if (!blk) { printf("rain: no 0x8003B600 tables in %s\n", path); return 0; }
    tab_parse(blk, blen, "Rain", &R.rain);
    tab_parse(blk, blen, "Wind", &R.wind);
    tab_parse(blk, blen, "Night Raining Fog", &R.fog);
    tab_parse(blk, blen, "Fog Blend Distance", &R.fogdist);
    tab_parse(blk, blen, "Ambience", &R.amb);
    tab_parse(blk, blen, "Volumes", &R.vols);
    free(blk);
    printf("rain: Ambience %s (%d zones), Volumes %s\n", R.amb.ok ? "loaded" : "missing", R.amb.entries,
           R.vols.ok ? "loaded" : "missing");
    if (R.rain.values < 2) R.rain.ok = 0;
    if (R.wind.values < 2) R.wind.ok = 0;
    printf("rain: %s Rain %s (%d entries), Wind %s, Night Raining Fog %s (%d values), Fog Blend Distance %.0f\n",
           path, R.rain.ok ? "loaded" : "missing", R.rain.entries, R.wind.ok ? "loaded" : "missing",
           R.fog.ok ? "loaded" : "missing", R.fog.values,
           R.fogdist.ok ? tab_entry(&R.fogdist, 0, 0)[0] : 0.0f);
    if (R.rain.ok)
        for (int i = 0; i < R.rain.entries; i++) {
            const float *v = (const float *)(R.rain.pal + (size_t)i * (size_t)R.rain.stride);
            printf("rain:   entry %d intensity %.2f threshold %.2f\n", i, v[0], v[1]);
        }
    return R.rain.ok;
}

/* EAXS_PFController::GetAmbientZone 0x46b330, in game: the AmbientAccessor
   (0x82c71c) and VolumeAccessor (0x82beac) CaptureData at the player's car
   (x, y), then GetDataInt(0) each */
int rain_ambience_at(float x, float y, int *zone, int *vol_cdb)
{
    int found = 0;
    if (R.amb.ok && R.amb.values >= 1) { int v; memcpy(&v, tab_entry(&R.amb, x, y), 4); *zone = v; found = 1; }
    if (R.vols.ok && R.vols.values >= 1) { int v; memcpy(&v, tab_entry(&R.vols, x, y), 4); *vol_cdb = v; }
    return found;
}

/* ---- when it rains ---- */
static float raining_at(float x, float test_track)   /* IsRainingAt 0x6134e0 */
{
    if (test_track) return 1.0f;
    return fabsf(0.5f * bcos_u(deg_units(x / 50.0f + R.clock)) + 0.5f);
}
static int g_test_track;
float rain_desired_at(float x, float y)   /* GetDesiredRainIntensity 0x61e380 */
{
    if (R.force >= 0.0f) return R.force;   /* test aid */
    if (!R.rain.ok) return 0.0f;
    const float *e = tab_entry(&R.rain, x, y);
    float f = raining_at(x, (float)g_test_track);
    return e[1] < f ? 0.0f : e[0];
}

/* ---- Rain::Wind 0x6136b0 ---- */
static void rain_wind(float dt, const float eye[3])
{
    if (R.wind.ok) {
        const float *w = tab_blend(&R.wind, &R.wind_b, eye[0], eye[1], 50.0f);
        int a = deg_units(w[1]);
        R.wvec[0] = bsin_u(a); R.wvec[1] = bcos_u(a); R.wvec[2] = 0.0f;
    }
    for (int i = 0; i < 3; i++) R.wvec[i] *= 0.01f;
    to_car(R.wvec, R.wvec);
    if (R.gust_state == 2) {
        R.gust_period = rnd(5.0f) + 1.0f; R.gust_timer = 0.0f; R.gust_state = 1;
        R.gust_tgt[0] = (rnd(0.05f) - 0.025f) * 25.0f + R.wvec[0];
        R.gust_tgt[1] = (rnd(0.05f) - 0.025f) * 25.0f + R.wvec[1];
        R.gust_tgt[2] = 0.0f + R.wvec[2];
    } else if (R.gust_state == 1) {
        R.gust[0] += (R.gust_tgt[0] - R.gust[0]) * dt;
        R.gust[1] += (R.gust_tgt[1] - R.gust[1]) * dt;
        R.gust_lerp += dt;   /* 0x8a44e4 */
        if (R.gust_lerp >= 1.0f) { R.gust_lerp = 0.0f; R.gust_state = 0; }
    } else {
        R.gust_timer += dt;
        if (R.gust_timer >= R.gust_period) R.gust_state = 2;
    }
}

/* ---- Rain::Update 0x61e700 ---- */
static void rain_step(const RainFrame *f, float dt)
{
    memcpy(R.R, f->car_R, sizeof R.R);
    rain_wind(dt, f->eye);
    /* SetIntensity 0x6139b0 + Change 0x613a30 for types 1, 0, 4 */
    R.want[5] = (unsigned)ftol((1.0f - R.I) * 400.0f);
    unsigned act = 400u - R.want[5];
    R.want[1] = (unsigned)ftol((float)act * R.frac[1]);
    R.want[0] = (unsigned)ftol((float)act * R.frac[0]);
    R.want[4] = (unsigned)ftol((float)act * R.frac[4]);
    /* the camera's motion in the car's axes, x -0.02, sideways dropped --
       only while car +0x18 -> +0x238 -> +0xc == 1, else 0 (0x61e8a1). Decoded
       G2 10-31: car +0x18 = its Player (ChangeCameraMover 0x5f1fd0 passes
       player +0x18 as the car to every mover), player +0x238 = the current
       CameraMover (set only by ChangeCameraMover / the ctor 0x5f187c), mover
       +0xc = its CameraMoverTypes (CameraMover ctor 0x44ab90 stores arg 2
       there; CubicCameraMover 0x451660 passes 1). So: the normal driving
       views only, not NIS / ICE, the jump cam's TrackCar, the photo view.
       The other branch zeroes the vector only: +0x5090 (camspeed) keeps its
       last value (written only inside the test, 0x61e949 / 0x61e969). */
    if (!f->cubic_cam) {
        memset(R.camv, 0, sizeof R.camv);
    } else {
        float v[3]; to_car(v, R.eye_vel);
        for (int i = 0; i < 3; i++) v[i] *= -0.02f;
        v[1] = 0.0f;
        float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        R.camspeed = l;
        if (l > 1.0f) { R.camspeed = 1.0f; vnorm(v); }
        memcpy(R.camv, v, sizeof v);
    }
    /* the rain space: RotZ(heading) at the car. car +0x90 is its forward
       row (confirmed: Car::GetForwardAcceleration 0x5ec3a0 dots +0x80
       with +0x90). Retail's 0xfbb4 (not 0x10000) for y < 0 kept. */
    float fw[3] = { f->car_R[0], f->car_R[1], f->car_R[2] };
    vnorm(fw);
    float fx = fw[0] > 1.0f ? 1.0f : fw[0] < -1.0f ? -1.0f : fw[0];
    int a = 0x4000 - (int)(asinf(fx) * (65536.0f / PI2));
    if (fw[1] < 0.0f) a = 0xfbb4 - a;
    a &= 0xffff;
    R.ca = bcos_u(a); R.sa = bsin_u(a);
    memcpy(R.carpos, f->car_pos, sizeof R.carpos);
    float turn = 0.0f;
    if (R.have_heading) {
        unsigned short dd = (unsigned short)(R.last_heading - (unsigned short)a);
        turn = (float)dd;
        if (turn >= 32768.0f) turn -= 65536.0f;
        if (turn > 400.0f) turn = 400.0f;
        if (turn < -400.0f) turn = -400.0f;
    }
    R.last_heading = (unsigned short)a; R.have_heading = 1;
    /* swap the buffers: write the older one */
    int prevb = R.cur; R.cur ^= 1; int curb = R.cur;
    /* the desired counts must total 400 */
    {
        unsigned sum = 0;
        for (int t = 0; t < 6; t++) sum += R.want[t];
        unsigned slack = 400u - sum;
        if (slack)
            for (int t = 0; t < 6; t++) if (R.want[t] >= slack) { R.want[t] -= slack; break; }
    }
    int all_eq = 1;
    for (int t = 0; t < 6; t++) if (R.count[t] != R.want[t]) { all_eq = 0; break; }
    /* TrackPathManager::FindZone 0x20 / 0x40 above the car and 9.5 m ahead */
    R.cover_car = R.cover_ahead = 0;
    if (f->cover) {
        float p[3] = { f->car_pos[0], f->car_pos[1], f->car_pos[2] };
        R.cover_car = f->cover(p);
        float q[3] = { f->car_pos[0] + fw[0] * 9.5f, f->car_pos[1] + fw[1] * 9.5f, f->car_pos[2] };
        R.cover_ahead = f->cover(q);
    }
    for (int i = 0; i < NDROPS; i++) {
        RDrop *p = &R.d[i];
        float *c = p->pos[curb], *o = p->pos[prevb];
        if (p->state == 0) {
            float k = turn * 5e-5f;
            c[1] += k * c[0];
            o[1] += k * o[0];
            float s = R.size[p->type * 2 + p->sub];
            const float *w = R.windv[p->wind];
            c[0] = o[0] + R.camv[0] + w[0] + s * R.gust[0];
            c[1] = o[1] + R.camv[1] + w[1] + s * R.gust[1];
            c[2] = o[2] + R.camv[2] + w[2] + s * R.gust[2];
            if (c[2] < 0.0f) p->state = 1;
            else if (c[0] > 18.0f) { c[0] -= 20.0f; o[0] -= 20.0f; }
            else if (c[0] < -2.0f) { c[0] += 20.0f; o[0] += 20.0f; }
            else if (c[1] > 3.5f) { c[1] -= 7.0f; o[1] -= 7.0f; }
            else if (c[1] < -3.5f) { c[1] += 7.0f; o[1] += 7.0f; }
            p->front = 0;
            if (c[0] >= 0.0f) {
                float n[3] = { c[0], c[1], c[2] }; vnorm(n);
                if (n[0] >= 0.96f) p->front = 1;
            }
        } else if (p->state < 3) {
            if (!all_eq) {
                for (int u = 0; u < 6; u++)
                    if (u != p->type && R.count[u] < R.want[u]) {
                        R.count[p->type]--; p->type = (unsigned char)u; R.count[u]++;
                        break;
                    }
            }
            if (p->type != 5 && p->type != 4) {
                p->front = 0;
                if (p->state == 1) { c[2] = 6.0f; memcpy(o, c, sizeof p->pos[0]); }
                p->state = 0;
            }
        }
        if ((R.cover_ahead && c[0] > 9.5f) || (R.cover_car && c[0] <= 9.5f)) {
            R.count[p->type]--; p->type = 5; R.count[5]++; p->state = 2;
        }
    }
}

/* OnScreenRain::Update 0x6140f0 */
static void screen_step(float dt, int paused)
{
    float spd = sqrtf(R.eye_vel[0] * R.eye_vel[0] + R.eye_vel[1] * R.eye_vel[1] + R.eye_vel[2] * R.eye_vel[2]) * 1e-4f;
    R.nscreen = R.cover_car ? 0 : (R.I != 0.0f ? NSCREEN : 0);
#if RAIN_VITA_CAP
    R.nscreen /= 2;
#endif
    for (int i = 0; i < R.nscreen; i++) {
        RScreenDrop *s = &R.sd[i];
        if (!paused) s->life -= dt;
        if (s->life > 0.0f) {
            s->y += 0.2f * s->speed * dt;
            if (s->y > 1.0f || s->x > 1.0f || s->x < 0.0f) s->life = 0.0f;
            else {
                float n[3] = { s->x - 0.5f, s->y - 0.1f, 0.0f }; vnorm(n);
                s->x += n[0] * spd;
            }
        } else {
            s->life = s->maxlife;
            s->x = rnd(1.0f); s->y = rnd(1.0f);
        }
    }
}

void rain_update(const RainFrame *f)
{
    if (!R.ready || !f || !f->in_game) { R.nscreen = 0; return; }
    g_test_track = f->test_track;
    /* the camera's velocity (camera +0x1e0) from its motion */
    if (R.have_eye && f->dt > 0.0f) {
        float v[3], l = 0;
        for (int i = 0; i < 3; i++) { v[i] = (f->eye[i] - R.last_eye[i]) / f->dt; l += v[i] * v[i]; }
        if (l > 200.0f * 200.0f) memset(v, 0, sizeof v);   /* a cut / teleport */
        memcpy(R.eye_vel, v, sizeof v);
    } else if (!R.have_eye) memset(R.eye_vel, 0, sizeof R.eye_vel);
    memcpy(R.last_eye, f->eye, sizeof R.last_eye); R.have_eye = 1;
    memcpy(R.camdir, f->fwd, sizeof R.camdir);
    if (f->paused) return;   /* GameFlowManager::IsPaused: drawn as it stands */
    int steps = f->steps < 1 ? 1 : f->steps > 4 ? 4 : f->steps;
    float dt = f->dt / (float)steps;
    if (fabsf(dt) < 0.009f) return;
    for (int k = 0; k < steps; k++) {
        R.clock += dt;
        screen_step(dt, 0);
        /* the rain switch 0x7faa50 is a static 1: nothing in SPEED2.EXE writes
           it and no options screen points at it, so retail always rains */
        R.wet_target = R.I != 0.0f ? 1.0f : 0.0f;
        R.wet += (R.wet_target - R.wet) * (R.wet < R.wet_target ? 0.05f : 0.0015f);
        if (R.rain.ok || R.force >= 0.0f) R.desired = rain_desired_at(f->eye[0], f->eye[1]);
        R.I += (R.desired - R.I) * dt;
        if (R.desired == 0.0f && R.I < 0.01f) R.I = 0.0f;
        if (R.I > 1.0f) R.I = 1.0f;
        R.frac[0] = 1.0f;
        rain_step(f, dt);
    }
    int raining = R.I > 0.0f;
    if (raining != R.was_raining) {
        printf("rain: %s at (%.0f, %.0f), desired %.2f, clock %.1f s\n", raining ? "starts" : "stops",
               f->eye[0], f->eye[1], R.desired, R.clock);
        R.was_raining = raining;
    }
    if (R.log) {
        R.logt += f->dt;
        if (R.logt >= 2.0f) {
            R.logt = 0.0f;
            printf("rain: I %.3f desired %.2f wet %.2f drops %u/%u screen %d cover %d/%d gust %.2f %.2f clock %.1f\n",
                   R.I, R.desired, R.wet, R.count[0], R.want[0], R.nscreen, R.cover_car, R.cover_ahead,
                   R.gust[0], R.gust[1], R.clock);
        }
    }
}

/* ---- Rain::Render 0x613a70 (batched quads 0x5bb430) ---- */
typedef struct { float p[3], uv[2]; unsigned char c[4]; } RVert;
int rain_draw(const float PV[16])
{
    if (!R.ready || !R.prog || !R.tex || R.count[0] + R.count[1] + R.count[2] + R.count[3] == 0) return 0;
    static RVert v[NDROPS * 4];
    /* the camera's direction in the car's axes */
    float cd[3]; to_car(cd, R.camdir);
    int nq = 0;
    int cap = RAIN_VITA_CAP > 0 ? RAIN_VITA_CAP : NDROPS;
    for (int t = 0; t < 4 && nq < cap; t++) {
        /* only type 0 (RAINDROP) is ever wanted: Change gives types 1 and 4
           a fraction of 0, 2 and 3 none; their textures are not ported */
        if (t != 0) continue;
        for (int i = 0; i < NDROPS && nq < cap; i++) {
            const RDrop *p = &R.d[i];
            if (p->type != t || p->state != 0) continue;   /* the rest are degenerate quads in retail */
            const float *P = p->pos[R.cur], *O = p->pos[R.cur ^ 1];
            float s[3], c[3], len;
            if (!R.velflag[t]) {
                s[0] = 0; s[1] = 0; s[2] = -1.0f;
                vcross(c, cd, s);
                len = R.streak[t];
            } else {
                s[0] = O[0] - P[0]; s[1] = O[1] - P[1]; s[2] = O[2] - P[2];
                vnorm(s);
                vcross(c, cd, s);
                len = R.streak[t] + R.camspeed;
            }
            for (int k = 0; k < 3; k++) { c[k] *= R.side[t]; s[k] *= len; }
            /* rain space -> world */
            float lp[4][3] = {
                { P[0] - s[0] - c[0], P[1] - s[1] - c[1], P[2] - s[2] - c[2] },
                { P[0] - s[0] + c[0], P[1] - s[1] + c[1], P[2] - s[2] + c[2] },
                { P[0] + s[0] + c[0], P[1] + s[1] + c[1], P[2] + s[2] + c[2] },
                { P[0] + s[0] - c[0], P[1] + s[1] - c[1], P[2] + s[2] - c[2] } };
            /* the batched path (0x803abc is a static 2, only compared at 0x613bc4 /
               0x614053): the sprite at esp+0xb0 is built by the eSprite ctor
               0x5bb3b0 (u0 v0 = 0, u1 v1 = 1, never overwritten by Render) and
               0x5bb430 emits P+c+s (u0 v0), P-c+s (u1 v0), P-c-s (u1 v1),
               P+c-s (u0 v1), c = sprite +0x30 (the side), s = +0x40 (the streak).
               Confirmed G2 10-31 (was the CPU path's order: u mirrored). */
            static const float uv[4][2] = { { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 } };
            unsigned char col[4];
            if (p->front) { col[0] = col[1] = col[2] = 0x80; col[3] = 0x80; }
            else { col[0] = col[1] = col[2] = 100; col[3] = 0x1c; }
            RVert *q = v + nq * 4;
            for (int k = 0; k < 4; k++) {
                q[k].p[0] = R.carpos[0] + lp[k][0] * R.ca - lp[k][1] * R.sa;
                q[k].p[1] = R.carpos[1] + lp[k][0] * R.sa + lp[k][1] * R.ca;
                q[k].p[2] = R.carpos[2] + lp[k][2];
                q[k].uv[0] = uv[k][0]; q[k].uv[1] = uv[k][1];
                memcpy(q[k].c, col, 4);
            }
            nq++;
        }
    }
    if (!nq) return 0;
    glUseProgram(R.prog);
    glUniformMatrix4fv(R.uMVP, 1, GL_FALSE, PV);
    glUniform1i(R.uTex, 0);
    if (R.uFogColor >= 0) {
        glUniform3fv(R.uFogColor, 1, R.fog_col);
        glUniform1f(R.uFogDensity, R.fog_den);
        glUniform1f(R.uFogK, R.fog_k);
        glUniform1f(R.uFogFloor, R.fog_floor);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.tex);
    glDisableVertexAttribArray(5 - RENDER_PARTICLE_COLOR_LOC);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, R.ibo);
    glBindBuffer(GL_ARRAY_BUFFER, R.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(RVert) * 4 * (size_t)nq), v, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RVert), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RVert), (void *)12);
    glVertexAttribPointer(RENDER_PARTICLE_COLOR_LOC, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RVert), (void *)20);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, R.blend == 2 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glDrawElements(GL_TRIANGLES, nq * 6, GL_UNSIGNED_SHORT, 0);
    glDisableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return nq;
}

/* ---- the screen drops (0x5c7ab0 with rain_drop.fx) ---- */
#ifdef __vita__
/* Vita perf: the scene copy on the GPU. While the 3D frame renders into the
   scene texture (render_scene_target), that texture is drawn 1:1 into R.ctex
   and the scene framebuffer is bound again; the frame's pixels are the same
   as glCopyTexSubImage2D's, which vitaGL runs as glReadPixels (end scene +
   wait for the GPU) + glTexSubImage2D (CPU convert + upload): ~50 ms a frame
   on HW 10-31. Costs one extra scene and a 720x408 copy pass. 1 = done. */
static int scene_copy_gpu(const GLint vp[4])
{
    GLuint fbo = 0, tex = 0; int w = 0, h = 0;
    if (R.cfailed || !R.cprog || !render_scene_target(&fbo, &tex, &w, &h)) return 0;
    if (vp[0] != 0 || vp[1] != 0 || vp[2] != w || vp[3] != h) return 0;   /* whole frame only */
    if (!R.cfbo || R.cw != w || R.ch != h) {
        if (R.cfbo) { glDeleteFramebuffers(1, &R.cfbo); glDeleteTextures(1, &R.ctex); R.cfbo = R.ctex = 0; }
        glGenTextures(1, &R.ctex);
        glBindTexture(GL_TEXTURE_2D, R.ctex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &R.cfbo);
        glBindFramebuffer(GL_FRAMEBUFFER, R.cfbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.ctex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glDeleteFramebuffers(1, &R.cfbo); glDeleteTextures(1, &R.ctex); R.cfbo = R.ctex = 0;
            R.cfailed = 1;
            printf("rain: GPU scene copy unavailable (framebuffer incomplete); glCopyTexSubImage2D\n");
            return 0;
        }
        R.cw = w; R.ch = h;
        printf("rain: screen drops read a GPU copy of the scene (%dx%d)\n", w, h);
    } else glBindFramebuffer(GL_FRAMEBUFFER, R.cfbo);
    GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
    if (sc) glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(R.cprog);
    glUniform1i(glGetUniformLocation(R.cprog, "uScene"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    static const float q[16] = { -1, -1, 0, 0,  1, -1, 1, 0,  -1, 1, 0, 1,  1, 1, 1, 1 };
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, R.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof q, q, GL_STREAM_DRAW);
    glDisableVertexAttribArray(2); glDisableVertexAttribArray(3);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);   /* back to the frame: the drops draw over it */
#ifdef OPENUG2_SIM_PRE_H
    {   /* test aid OPENUG2_RAIN_COPYCHECK: every 60th copy, scene vs copy pixel diff */
        static int nchk;
        unsigned char *a = NULL, *b2 = NULL;
        if (getenv("OPENUG2_RAIN_COPYCHECK") && ++nchk % 60 == 30 &&
            (a = (unsigned char *)malloc((size_t)w * h * 4)) && (b2 = (unsigned char *)malloc((size_t)w * h * 4))) {
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, a);
            glBindFramebuffer(GL_FRAMEBUFFER, R.cfbo);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, b2);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            int mx = 0; long nd = 0; unsigned long sum = 0;
            for (long i = 0; i < (long)w * h; i++) for (int k = 0; k < 3; k++) {
                int d = abs((int)a[i*4+k] - (int)b2[i*4+k]); if (d > mx) mx = d; if (d) nd++; sum += a[i*4+k]; }
            printf("rain copycheck: %dx%d max diff %d, %ld channels differ, scene mean %.1f\n", w, h, mx, nd, (double)sum / (w * h * 3.0));
        }
        free(a); free(b2);
    }
#endif
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (sc) glEnable(GL_SCISSOR_TEST);
    return 1;
}
#endif

int rain_draw_screen(void)
{
    if (!R.ready || !R.sprog || !R.stex || R.nscreen <= 0) return 0;
    {   /* nothing to draw (every drop's maxlife 0): no scene copy either */
        int any = 0;
        for (int i = 0; i < R.nscreen && !any; i++) any = R.sd[i].maxlife > 0.0f;
        if (!any) return 0;
    }
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return 0;
#ifdef __vita__
    if (scene_copy_gpu(vp)) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, R.ctex);
    } else
#endif
    {
    glActiveTexture(GL_TEXTURE1);
    if (!R.scene || R.scene_w != vp[2] || R.scene_h != vp[3]) {
        if (!R.scene) glGenTextures(1, &R.scene);
        glBindTexture(GL_TEXTURE_2D, R.scene);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, vp[2], vp[3], 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        R.scene_w = vp[2]; R.scene_h = vp[3];
    } else glBindTexture(GL_TEXTURE_2D, R.scene);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);   /* the scene copy */
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.stex);
    glUseProgram(R.sprog);
    glUniform1i(R.sDrop, 0); glUniform1i(R.sScene, 1);
    glDisableVertexAttribArray(2); glDisableVertexAttribArray(3);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    /* a strip a drop: (X,Y) uv 0,0; (X+h,Y) 1,0; (X,Y+h) 0,1; (X+h,Y+h) 1,1.
       Confirmed G2 10-31 from the asm of 0x5c7ab0 (0x5c7ce5..0x5c7d90: X = 2x - 1,
       Y = 1 - 2y, h = size x 0.12, DrawPrimitiveUP(TRIANGLESTRIP, 2, stride 0x18),
       RAINDROPOFFSET = (x - h/4, y + h/4, h, -h) at 0x5c7cdf): clip-space y is up in
       both APIs, so the strip is the same; only the scene copy's v differs (a D3D
       render-target copy has v = 0 at the top, a GL framebuffer copy at the bottom),
       hence the shader's 1 - s.y. */
    static float q[NSCREEN * 16];
    for (int i = 0; i < R.nscreen; i++) {
        const RScreenDrop *s = &R.sd[i];
        float h = s->size * 0.12f, X = 2.0f * s->x - 1.0f, Y = 1.0f - 2.0f * s->y;
        const float e[16] = { X, Y, 0, 0,  X + h, Y, 1, 0,  X, Y + h, 0, 1,  X + h, Y + h, 1, 1 };
        memcpy(q + i * 16, e, sizeof e);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, R.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(float) * 16 * (size_t)R.nscreen), q, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    int n = 0;
    for (int i = 0; i < R.nscreen; i++) {
        const RScreenDrop *s = &R.sd[i];
        if (s->maxlife <= 0.0f) continue;
        float h = s->size * 0.12f, w = 0.25f * h;
        glUniform4f(R.sOff, s->x - w, s->y + w, h, -h);   /* RAINDROPOFFSET */
        glUniform1f(R.sAlpha, s->life / s->maxlife * 3.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, i * 4, 4);
        n++;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    return n;
}

float rain_intensity(void) { return R.I; }
int rain_has_draws(void)
{
    return R.ready && (R.count[0] + R.count[1] + R.count[2] + R.count[3] > 0 || R.nscreen > 0);
}
float rain_wetness(void) { return R.ready ? R.wet : 0.0f; }

int rain_fog_values(float x, float y, float out[8])
{
    if (!R.fog.ok) return 0;
    float dist = R.fogdist.ok ? tab_entry(&R.fogdist, 0, 0)[0] : 50.0f;
    if (dist <= 0.0f) dist = 50.0f;
    const float *v = tab_blend(&R.fog, &R.fog_b, x, y, dist);
    memset(out, 0, sizeof(float) * 8);
    for (int i = 0; i < R.fog.values && i < 8; i++) out[i] = v[i];
    return R.fog.values;
}
void rain_set_fog_colour(int c0, int c1, int c2) { R.fogc[0] = c0; R.fogc[1] = c1; R.fogc[2] = c2; }

/* 0x5c7860, slot 2 (AddScreenEffect(2, {fog colour, 128, I, 0})): each
   channel ((c / 128 - 1) x I + 1) x 127.5, applied as 2 x c / 255 */
int rain_tint(float mul[3])
{
    if (!R.ready || R.I < 0.01f) return 0;
    for (int k = 0; k < 3; k++) {
        int c = ftol((((float)R.fogc[k] * 0.0078125f - 1.0f) * R.I + 1.0f) * 127.5f);
        mul[k] = 2.0f * (float)c / 255.0f;
    }
    return 1;
}
