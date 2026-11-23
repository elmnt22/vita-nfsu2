/* boot_settings.inc -- the Settings screen opened from the LiveArea button
   (template.xml frame3 settings.png, psla:-settings) or by holding L+R while
   the game starts. It runs before the engine loads anything. Text uses the
   Vita's own system font (ScePvf; the simulator stands in a Windows font),
   rasterised into an atlas once. The layout follows the green Vita Settings
   app: inset outlined rows, white labels and checkboxes, a blue focus row,
   and a separate description below the list. Shapes use distance fields.
   Each setting is one of the switch files in ux0:data/NFSU2 the game
   already reads at boot:
     Logs        nolog.txt   present = log.txt and perf.csv are not written
                             (boot.txt and crumbs.txt are small and always kept)
     Frame rate  fps60.txt   present = unlocked (vsync, up to 60), else locked 30
     Fast travel fasttravel.txt  present = the World Map's GPS dialog "No"
                             moves the car to the entry (NFSU2FastTravel mod)
     Right stick look rstickcam.txt  present = the chase camera turns to the
                             right stick's direction (cut back when released
                             behind), bumper / hood look back with the stick's
                             lower half; off = the right stick's turning orbit
     Ignore Visual Rating novisrating.txt  present = the career's DVD covers
                             unlock without their star rating (GitHub #36, a
                             port option; the cover still pays by the rating)
     Trophies    trophies.txt  present = trophies reported to the system; off
                             by default (progress is still kept in the profile)
     Full detail fulldetail.txt  present = the original road reflection rate,
                             traffic count and particle rate; off by default
                             (the Vita's lighter mix for 30 fps downtown)
     World Expansion  expansion/enable.txt  present = Rockport (from NFS Most
                             Wanted) and Palmont (from NFS Carbon) in Free Run,
                             where the Prep Tool installed them; off by default
     Save logs as ZIP        packs the last (or all kept) run folders of
                             ux0:data/NFSU2/logs into ux0:data/NFSU2/
                             NFSU2_logs_<time>.zip for bug reports; local only
     Reset caches            deletes ux0:data/NFSU2/cache (converted world
                             tiles, shaders, shop/parts lists); profiles,
                             options and the game data are kept
   Rows that do not fit scroll (d-pad, or drag on the touch screen) rather
   than shrinking. Leaving the screen restarts the app, so the game boots
   from a clean state.

   The includer provides:
     BS_W, BS_H             screen size
     bs_pad()               buttons held now, STG_* bits
     bs_touch_now(&x, &y)   1 while the front touch screen is held (screen pixels)
     bs_fopen(p, mode)      stdio fopen of a ux0: path
     bs_list(dir, dirs, names, max)  names in dir: folders (dirs 1) or files (0)
     bs_stamp(out, cap)     local time as "YYYY-MM-DD_HH-MM-SS"
     bs_present()           show the frame and wait about 1/30 s
     bs_exists(p), bs_touch(p), bs_remove(p)
     bs_rmtree(p)           delete a folder tree, returns the files deleted
     bs_font_raster(face, px, cp, buf, stride, cap, &glyph)
                            one glyph of face 0 (regular) / 1 (bold) at an em
                            of px pixels into buf (8-bit coverage, top-left
                            at buf[0]); 0 = no glyph */

#include <stddef.h>
#include <math.h>

enum { STG_UP = 1, STG_DOWN = 2, STG_LEFT = 4, STG_RIGHT = 8, STG_CROSS = 16, STG_CIRCLE = 32,
       STG_START = 64, STG_L = 128, STG_R = 256 };

#define BS_NOLOG  "ux0:data/NFSU2/nolog.txt"
#define BS_FPS60  "ux0:data/NFSU2/fps60.txt"
#define BS_FTRAV  "ux0:data/NFSU2/fasttravel.txt"
#define BS_RSCAM  "ux0:data/NFSU2/rstickcam.txt"    /* = main.c rstick_look_on() */
#define BS_NOVR   "ux0:data/NFSU2/novisrating.txt"  /* = career.c career_vr_ignored() */
#define BS_TRP    "ux0:data/NFSU2/trophies.txt"     /* = trophy.h TROPHY_ON_FILE */
#define BS_FULL   "ux0:data/NFSU2/fulldetail.txt"   /* = main.c full_detail() */
#define BS_CACHE  "ux0:data/NFSU2/cache"
#define BS_XP     "ux0:data/NFSU2/expansion/enable.txt"   /* = xpcity.c XP_SWITCH */

/* one rasterised glyph: w x h coverage, left = pen to bitmap's left edge,
   top = baseline to bitmap's top edge (up positive), adv = pen advance */
typedef struct { int w, h, left, top; float adv; } BsGlyphRaster;

/* ---- glyph atlas ---------------------------------------------------------- */
#define BSF_FIRST 32
#define BSF_N     95
#define BSF_ATLAS 512
typedef struct { float u0, v0, u1, v1; short w, h, left, top; float adv; } BsGl;
typedef struct { GLuint tex; int px; float cap; BsGl g[BSF_N]; } BsFont;

static int bsf_build(BsFont *f, int face, int px)
{
    memset(f, 0, sizeof *f);
    f->px = px;
    unsigned char *atlas = (unsigned char *)calloc(BSF_ATLAS * BSF_ATLAS, 4);
    unsigned char *tmp = (unsigned char *)malloc(160 * 160);
    if (!atlas || !tmp) { free(atlas); free(tmp); return 0; }
    int x = 1, y = 1, rowh = 0;
    for (int i = 0; i < BSF_N; i++) {
        unsigned cp = BSF_FIRST + (unsigned)i;
        BsGlyphRaster r; memset(&r, 0, sizeof r);
        memset(tmp, 0, 160 * 160);
        if (!bs_font_raster(face, px, cp, tmp, 160, 160, &r)) continue;
        if (r.w > 158) r.w = 158;
        if (r.h > 158) r.h = 158;
        if (x + r.w + 2 > BSF_ATLAS) { x = 1; y += rowh + 2; rowh = 0; }
        if (y + r.h + 2 > BSF_ATLAS) break;
        for (int yy = 0; yy < r.h; yy++)
            for (int xx = 0; xx < r.w; xx++) {
                unsigned char *p = atlas + ((size_t)(y + yy) * BSF_ATLAS + (size_t)(x + xx)) * 4;
                p[0] = p[1] = p[2] = 255; p[3] = tmp[yy * 160 + xx];
            }
        BsGl *g = &f->g[i];
        g->u0 = (float)x / BSF_ATLAS; g->v0 = (float)y / BSF_ATLAS;
        g->u1 = (float)(x + r.w) / BSF_ATLAS; g->v1 = (float)(y + r.h) / BSF_ATLAS;
        g->w = (short)r.w; g->h = (short)r.h; g->left = (short)r.left; g->top = (short)r.top; g->adv = r.adv;
        if (cp == 'H') f->cap = (float)r.top;
        x += r.w + 2;
        if (r.h > rowh) rowh = r.h;
    }
    if (f->cap <= 0.0f) f->cap = 0.7f * (float)px;
    glGenTextures(1, &f->tex);
    glBindTexture(GL_TEXTURE_2D, f->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BSF_ATLAS, BSF_ATLAS, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(atlas); free(tmp);
    return 1;
}

/* ---- the frame's quads ------------------------------------------------------
   Per vertex: pixel position, atlas uv, colour, second colour (vertical
   gradient end), the shape's rectangle (x, y, w, h) and parameters (mode,
   corner radius, line width). Modes: 0 glyph, 1 filled rounded rectangle,
   2 rounded-rectangle outline, 3 cross (the PlayStation X), 4 ring, 5 tick. */
typedef struct { float x, y, u, v, c[4], c2[4], rc[4], pr[4]; } BsVert;
#define BSV_MAX (6 * 1200)
static BsVert *g_bsv; static int g_nbsv, g_bsv_tex;   /* tex: atlas bound for this batch */
static struct { GLuint prog, vbo[3]; int vbo_i; GLint uScreen, uTex; int ok; } BSR;

static const char *k_bs_vs =
#ifdef N2_GLES
    "precision highp float;\n"
#endif
    "attribute vec2 aPos; attribute vec2 aUV; attribute vec4 aCol; attribute vec4 aCol2;\n"
    "attribute vec4 aRect; attribute vec4 aPar;\n"
    "uniform vec2 uScreen;\n"
    "varying vec2 vUV; varying vec2 vPx; varying vec4 vCol; varying vec4 vCol2; varying vec4 vRect; varying vec4 vPar;\n"
    "void main(){ vUV=aUV; vPx=aPos; vCol=aCol; vCol2=aCol2; vRect=aRect; vPar=aPar;\n"
    "  gl_Position=vec4(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0, 0.0, 1.0); }\n";
static const char *k_bs_fs =
#ifdef N2_GLES
    "precision mediump float;\n"
#endif
    "uniform sampler2D uTex;\n"
    "varying vec2 vUV; varying vec2 vPx; varying vec4 vCol; varying vec4 vCol2; varying vec4 vRect; varying vec4 vPar;\n"
    "void main(){\n"
    "  float m=vPar.x;\n"
    "  if(m<0.5){ gl_FragColor=vec4(vCol.rgb, vCol.a*texture2D(uTex,vUV).a); return; }\n"
    "  vec2 hs=vRect.zw*0.5; vec2 p=vPx-(vRect.xy+hs);\n"
    "  float rad=vPar.y, lw=vPar.z;\n"
    "  vec2 q=abs(p)-(hs-vec2(rad));\n"
    "  float d=length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-rad;\n"
    "  float a;\n"
    "  if(m<1.5) a=clamp(0.5-d,0.0,1.0);\n"
    "  else if(m<2.5) a=clamp(0.5-d,0.0,1.0)*clamp(d+lw+0.5,0.0,1.0);\n"
    "  else if(m<3.5){ float dd=min(abs(p.x-p.y),abs(p.x+p.y))*0.70710678;\n"
    "    a=clamp(lw*0.5+0.5-dd,0.0,1.0)*clamp(hs.x+0.5-max(abs(p.x),abs(p.y)),0.0,1.0); }\n"
    "  else if(m<4.5){ float r=length(p); a=clamp(lw*0.5+0.5-abs(r-hs.x+lw*0.5),0.0,1.0); }\n"
    "  else { vec2 ta=vec2(-0.52,0.02)*hs.x, tb=vec2(-0.14,0.40)*hs.x, tc=vec2(0.56,-0.44)*hs.x;\n"
    "    vec2 pa=p-ta, ba=tb-ta; float d1=length(pa-ba*clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0));\n"
    "    vec2 pb=p-tb, cb=tc-tb; float d2=length(pb-cb*clamp(dot(pb,cb)/dot(cb,cb),0.0,1.0));\n"
    "    a=clamp(lw*0.5+0.5-min(d1,d2),0.0,1.0); }\n"
    "  float t=clamp((vPx.y-vRect.y)/max(vRect.w,1.0),0.0,1.0);\n"
    "  vec4 c=mix(vCol,vCol2,t);\n"
    "  gl_FragColor=vec4(c.rgb,c.a*a);\n"
    "}\n";

static GLuint bsr_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512] = ""; glGetShaderInfoLog(s, sizeof log, NULL, log);
               printf("settings: shader failed: %s\n", log); glDeleteShader(s); return 0; }
    return s;
}
static int bsr_init(void)
{
    g_bsv = (BsVert *)malloc(sizeof(BsVert) * BSV_MAX);
    GLuint vs = bsr_shader(GL_VERTEX_SHADER, k_bs_vs), fs = vs ? bsr_shader(GL_FRAGMENT_SHADER, k_bs_fs) : 0;
    if (!g_bsv || !vs || !fs) return 0;
    BSR.prog = glCreateProgram();
    glAttachShader(BSR.prog, vs); glAttachShader(BSR.prog, fs);
    glBindAttribLocation(BSR.prog, 0, "aPos"); glBindAttribLocation(BSR.prog, 1, "aUV");
    glBindAttribLocation(BSR.prog, 2, "aCol"); glBindAttribLocation(BSR.prog, 3, "aCol2");
    glBindAttribLocation(BSR.prog, 4, "aRect"); glBindAttribLocation(BSR.prog, 5, "aPar");
    glLinkProgram(BSR.prog);
    GLint ok = 0; glGetProgramiv(BSR.prog, GL_LINK_STATUS, &ok);
    if (!ok) { printf("settings: shader link failed\n"); return 0; }
    BSR.uScreen = glGetUniformLocation(BSR.prog, "uScreen");
    BSR.uTex = glGetUniformLocation(BSR.prog, "uTex");
    glGenBuffers(3, BSR.vbo);
    return BSR.ok = 1;
}
static void bsr_flush(void)
{
    if (!g_nbsv) return;
    GLuint vb = BSR.vbo[BSR.vbo_i]; BSR.vbo_i = (BSR.vbo_i + 1) % 3;
    glBindBuffer(GL_ARRAY_BUFFER, vb);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(BsVert) * (size_t)g_nbsv), g_bsv, GL_DYNAMIC_DRAW);
    glUseProgram(BSR.prog);
    glUniform2f(BSR.uScreen, (float)BS_W, (float)BS_H);
    glUniform1i(BSR.uTex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)g_bsv_tex);
    const size_t st = sizeof(BsVert);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)st, (void *)offsetof(BsVert, x));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)st, (void *)offsetof(BsVert, u));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, (GLsizei)st, (void *)offsetof(BsVert, c));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)st, (void *)offsetof(BsVert, c2));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, (GLsizei)st, (void *)offsetof(BsVert, rc));
    glEnableVertexAttribArray(5); glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, (GLsizei)st, (void *)offsetof(BsVert, pr));
    glDrawArrays(GL_TRIANGLES, 0, g_nbsv);
    for (int i = 0; i < 6; i++) glDisableVertexAttribArray((GLuint)i);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    g_nbsv = 0;
}
static void bsr_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                     const float c[4], const float c2[4], const float rc[4], float mode, float rad, float lw, int tex)
{
    if (tex && tex != g_bsv_tex) { bsr_flush(); g_bsv_tex = tex; }
    if (g_nbsv + 6 > BSV_MAX) bsr_flush();
    const float px[6] = { x0, x1, x0, x0, x1, x1 }, py[6] = { y0, y0, y1, y1, y0, y1 };
    const float pu[6] = { u0, u1, u0, u0, u1, u1 }, pv[6] = { v0, v0, v1, v1, v0, v1 };
    for (int i = 0; i < 6; i++) {
        BsVert *v = &g_bsv[g_nbsv++];
        v->x = px[i]; v->y = py[i]; v->u = pu[i]; v->v = pv[i];
        memcpy(v->c, c, sizeof v->c); memcpy(v->c2, c2 ? c2 : c, sizeof v->c2);
        memcpy(v->rc, rc, sizeof v->rc);
        v->pr[0] = mode; v->pr[1] = rad; v->pr[2] = lw; v->pr[3] = 0.0f;
    }
}

/* ---- drawing helpers (pixels, top-left origin) ---------------------------- */
#define BSC(r, g, b, a) ((const float[4]){ (r) / 255.0f, (g) / 255.0f, (b) / 255.0f, (a) })
static void bs_shape(float x, float y, float w, float h, float mode, float rad, float lw,
                     const float c[4], const float c2[4])
{
    const float rc[4] = { x, y, w, h };
    if (rad > 0.5f * w) rad = 0.5f * w;
    if (rad > 0.5f * h) rad = 0.5f * h;
    bsr_quad(x - 1, y - 1, x + w + 1, y + h + 1, 0, 0, 0, 0, c, c2, rc, mode, rad, lw, 0);
}
static void bs_fill(float x, float y, float w, float h, float rad, const float c[4], const float c2[4])
{ bs_shape(x, y, w, h, 1.0f, rad, 0.0f, c, c2); }
static void bs_line(float x, float y, float w, float h, float rad, float lw, const float c[4])
{ bs_shape(x, y, w, h, 2.0f, rad, lw, c, NULL); }
static float bs_text_w(const BsFont *f, const char *s)
{
    float w = 0;
    for (; *s; s++) { int i = (unsigned char)*s - BSF_FIRST; if (i >= 0 && i < BSF_N) w += f->g[i].adv; }
    return w;
}
/* text whose capital letters are centred on yc */
static void bs_text(const BsFont *f, const char *s, float x, float yc, const float c[4])
{
    const float rc[4] = { 0, 0, 1, 1 };
    float base = floorf(yc + 0.5f * f->cap + 0.5f);
    x = floorf(x + 0.5f);
    for (; *s; s++) {
        int i = (unsigned char)*s - BSF_FIRST;
        if (i < 0 || i >= BSF_N) continue;
        const BsGl *g = &f->g[i];
        if (g->w && g->h) {
            float gx = x + g->left, gy = base - g->top;
            bsr_quad(gx, gy, gx + g->w, gy + g->h, g->u0, g->v0, g->u1, g->v1, c, NULL, rc, 0.0f, 0, 0, (int)f->tex);
        }
        x += g->adv;
    }
}
/* White face-button symbols used by the system footer. */
static void bs_face_button(float cx, float cy, float r, int cross)
{
    float s = r * 0.65f;
    if (cross) bs_shape(cx - s, cy - s, 2 * s, 2 * s, 3.0f, 0, 1.8f * r / 9.0f, BSC(255, 255, 255, 1.0f), NULL);
    else bs_shape(cx - s, cy - s, 2 * s, 2 * s, 4.0f, 0, 1.8f * r / 9.0f, BSC(255, 255, 255, 1.0f), NULL);
}

/* ---- Save logs as ZIP (user 11-19) ----------------------------------------
   Packs the kept run folders of ux0:data/NFSU2/logs (named by their start
   time, see logs_rotate in sdl_vita.c) into one ZIP in ux0:data/NFSU2 that a
   player can attach to a bug report. "latest" is this Settings run and is
   left out: after a crash, the crashed run is the newest dated folder.
   Nothing is uploaded. Deflate is our own (LZ77 + the fixed Huffman codes of
   RFC 1951): the builds carry no zlib, and text logs still shrink ~5x. */
#define BS_LOGS    "ux0:data/NFSU2/logs"
#define BS_ZIPDIR  "ux0:data/NFSU2"
#define BSZ_WB     15
#define BSZ_W      (1 << BSZ_WB)
#define BSZ_HB     15
#define BSZ_CHAIN  24
#define BSZ_MAXE   96

typedef struct { unsigned char *p; size_t n, cap; unsigned bits; int nbits, err; } BszOut;
static void bsz_byte(BszOut *o, unsigned b) { if (o->n < o->cap) o->p[o->n++] = (unsigned char)b; else o->err = 1; }
static void bsz_bits(BszOut *o, unsigned v, int n)   /* LSB first, n <= 16 */
{
    o->bits |= v << o->nbits; o->nbits += n;
    while (o->nbits >= 8) { bsz_byte(o, o->bits & 255u); o->bits >>= 8; o->nbits -= 8; }
}
static void bsz_code(BszOut *o, unsigned code, int n)   /* Huffman codes go MSB first */
{
    unsigned r = 0;
    for (int i = 0; i < n; i++) { r = (r << 1) | (code & 1u); code >>= 1; }
    bsz_bits(o, r, n);
}
static void bsz_lit(BszOut *o, unsigned v)   /* the fixed literal/length code */
{
    if (v < 144) bsz_code(o, 0x30 + v, 8);
    else if (v < 256) bsz_code(o, 0x190 + v - 144, 9);
    else if (v < 280) bsz_code(o, v - 256, 7);
    else bsz_code(o, 0xc0 + v - 280, 8);
}
static const unsigned short k_bsz_lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char k_bsz_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                              3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short k_bsz_dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
                                                513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char k_bsz_dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
                                              8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static void bsz_match(BszOut *o, int len, int dist)
{
    int c = 0, d = 0;
    while (c < 28 && k_bsz_lbase[c + 1] <= len) c++;
    while (d < 29 && k_bsz_dbase[d + 1] <= dist) d++;
    bsz_lit(o, 257u + (unsigned)c);
    if (k_bsz_lext[c]) bsz_bits(o, (unsigned)(len - k_bsz_lbase[c]), k_bsz_lext[c]);
    bsz_code(o, (unsigned)d, 5);
    if (k_bsz_dext[d]) bsz_bits(o, (unsigned)(dist - k_bsz_dbase[d]), k_bsz_dext[d]);
}
/* one fixed-Huffman block over src; prog(user, bytes done) != 0 cancels.
   Returns the compressed size, 0 when it did not fit (caller stores raw),
   -1 when cancelled or out of memory. */
static long bsz_deflate(const unsigned char *src, size_t n, unsigned char *dst, size_t cap,
                        int (*prog)(void *, size_t), void *user)
{
    int *head = (int *)malloc(sizeof(int) << BSZ_HB), *prev = (int *)malloc(sizeof(int) * BSZ_W);
    if (!head || !prev) { free(head); free(prev); return -1; }
    for (int i = 0; i < (1 << BSZ_HB); i++) head[i] = -1;
    for (int i = 0; i < BSZ_W; i++) prev[i] = -1;
    BszOut o = { dst, 0, cap, 0, 0, 0 };
    bsz_bits(&o, 1, 1); bsz_bits(&o, 1, 2);   /* BFINAL, BTYPE 01 = fixed codes */
    size_t pos = 0, tick = 1u << 20;
    long result = 0;
#define BSZ_HASH(q) ((((unsigned)(q)[0] | ((unsigned)(q)[1] << 8) | ((unsigned)(q)[2] << 16)) * 2654435761u) >> (32 - BSZ_HB))
#define BSZ_INSERT(at) do { unsigned h_ = BSZ_HASH(src + (at)); prev[(at) & (BSZ_W - 1)] = head[h_]; head[h_] = (int)(at); } while (0)
    while (pos < n) {
        if (pos >= tick) {
            tick += 1u << 20;
            if (prog && prog(user, pos)) { result = -1; goto done; }
        }
        int best = 0, bdist = 0;
        if (pos + 3 <= n) {
            int maxlen = n - pos < 258 ? (int)(n - pos) : 258;
            int cand = head[BSZ_HASH(src + pos)], chain = BSZ_CHAIN;
            while (cand >= 0 && (int)pos - cand < BSZ_W && chain--) {
                const unsigned char *a = src + cand, *b = src + pos;
                if (a[best] == b[best]) {
                    int l = 0;
                    while (l < maxlen && a[l] == b[l]) l++;
                    if (l > best) { best = l; bdist = (int)pos - cand; if (l == maxlen) break; }
                }
                int nx = prev[cand & (BSZ_W - 1)];
                if (nx >= cand) break;   /* a slot reused by a newer position: the chain ends */
                cand = nx;
            }
            BSZ_INSERT(pos);
        }
        if (best >= 3) {
            bsz_match(&o, best, bdist);
            for (int k = 1; k < best; k++)
                if (pos + (size_t)k + 3 <= n) BSZ_INSERT(pos + (size_t)k);
            pos += (size_t)best;
        } else {
            bsz_lit(&o, src[pos]);
            pos++;
        }
        if (o.err) goto done;   /* bigger than the space given: stored instead */
    }
#undef BSZ_INSERT
#undef BSZ_HASH
    bsz_lit(&o, 256);
    if (o.nbits) bsz_byte(&o, o.bits & 255u);
    result = o.err ? 0 : (long)o.n;
done:
    free(head); free(prev);
    return result;
}
static unsigned bsz_crc(unsigned crc, const unsigned char *p, size_t n)
{
    static unsigned t[256]; static int ready;
    if (!ready) {
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    while (n--) crc = t[(crc ^ *p++) & 255u] ^ (crc >> 8);
    return ~crc;
}

typedef struct { char name[96]; unsigned crc, csize, usize, off; unsigned short method, dtime, ddate; } BszEntry;
typedef struct {
    FILE *f; unsigned off; int err;
    BszEntry e[BSZ_MAXE]; int ne;
    unsigned short dtime, ddate;
} BszZip;
static void bsz_put(BszZip *z, const void *p, size_t n)
{
    if (!z->err && n && fwrite(p, 1, n, z->f) != n) z->err = 1;
    z->off += (unsigned)n;
}
static void bsz_le(unsigned char *b, unsigned v, int n) { for (int i = 0; i < n; i++) b[i] = (unsigned char)(v >> (8 * i)); }
/* DOS date/time from a run folder name "YYYY-MM-DD_HH-MM-SS..." */
static void bsz_dostime(const char *stamp, unsigned short *dt, unsigned short *dd)
{
    int y = 1980, mo = 1, d = 1, h = 0, mi = 0, s = 0;
    if (sscanf(stamp, "%4d-%2d-%2d_%2d-%2d-%2d", &y, &mo, &d, &h, &mi, &s) < 3 || y < 1980) { y = 1980; mo = d = 1; }
    *dd = (unsigned short)(((y - 1980) << 9) | (mo << 5) | d);
    *dt = (unsigned short)((h << 11) | (mi << 5) | (s / 2));
}
/* one member: deflated when that is smaller, else stored */
static int bsz_add(BszZip *z, const char *name, const unsigned char *data, size_t n,
                   int (*prog)(void *, size_t), void *user)
{
    if (z->ne >= BSZ_MAXE || z->err) return 0;
    BszEntry *e = &z->e[z->ne];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    e->crc = bsz_crc(0, data, n); e->usize = (unsigned)n; e->off = z->off;
    e->dtime = z->dtime; e->ddate = z->ddate;
    size_t cap = n + n / 8 + 64;
    unsigned char *c = n ? (unsigned char *)malloc(cap) : NULL;
    long cn = c ? bsz_deflate(data, n, c, cap, prog, user) : 0;
    if (cn < 0) { free(c); return -1; }
    const unsigned char *body = data;
    e->method = 0; e->csize = (unsigned)n;
    if (cn > 0 && (size_t)cn < n) { body = c; e->method = 8; e->csize = (unsigned)cn; }
    unsigned char h[30];
    bsz_le(h, 0x04034b50u, 4); bsz_le(h + 4, 20, 2); bsz_le(h + 6, 0, 2); bsz_le(h + 8, e->method, 2);
    bsz_le(h + 10, e->dtime, 2); bsz_le(h + 12, e->ddate, 2); bsz_le(h + 14, e->crc, 4);
    bsz_le(h + 18, e->csize, 4); bsz_le(h + 22, e->usize, 4);
    bsz_le(h + 26, (unsigned)strlen(e->name), 2); bsz_le(h + 28, 0, 2);
    bsz_put(z, h, sizeof h); bsz_put(z, e->name, strlen(e->name)); bsz_put(z, body, e->csize);
    free(c);
    z->ne++;
    return z->err ? 0 : 1;
}
static void bsz_finish(BszZip *z)
{
    unsigned cd = z->off;
    for (int i = 0; i < z->ne; i++) {
        const BszEntry *e = &z->e[i];
        unsigned char h[46];
        memset(h, 0, sizeof h);
        bsz_le(h, 0x02014b50u, 4); bsz_le(h + 4, 20, 2); bsz_le(h + 6, 20, 2); bsz_le(h + 10, e->method, 2);
        bsz_le(h + 12, e->dtime, 2); bsz_le(h + 14, e->ddate, 2); bsz_le(h + 16, e->crc, 4);
        bsz_le(h + 20, e->csize, 4); bsz_le(h + 24, e->usize, 4); bsz_le(h + 28, (unsigned)strlen(e->name), 2);
        bsz_le(h + 42, e->off, 4);
        bsz_put(z, h, sizeof h); bsz_put(z, e->name, strlen(e->name));
    }
    unsigned char t[22];
    memset(t, 0, sizeof t);
    bsz_le(t, 0x06054b50u, 4); bsz_le(t + 8, (unsigned)z->ne, 2); bsz_le(t + 10, (unsigned)z->ne, 2);
    bsz_le(t + 12, z->off - cd, 4); bsz_le(t + 16, cd, 4);
    bsz_put(z, t, sizeof t);
}
static unsigned char *bsz_read(const char *path, size_t *n)
{
    *n = 0;
    FILE *f = bs_fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *p = len >= 0 ? (unsigned char *)malloc((size_t)len + 1) : NULL;
    if (p && fread(p, 1, (size_t)len, f) != (size_t)len) { free(p); p = NULL; }
    fclose(f);
    if (p) *n = (size_t)len;
    return p;
}
static long bsz_size(const char *path)
{
    FILE *f = bs_fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long len = ftell(f); fclose(f);
    return len;
}
static int bsz_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* ---- the screen ------------------------------------------------------------ */
enum { I_LOGS, I_EXPORT, I_FPS, I_FTRAV, I_RSCAM, I_NOVR, I_TROPHY, I_FULL, I_XPHDR, I_XP, I_RESET, I_START, I_N };
#define BS_HEADER(i) ((i) == I_XPHDR)   /* section titles: drawn, never selected */
typedef struct {
    int logs, unlocked, ftrav, rscam, novr, trophies, full, export_all, xp;
    int xp_city[2];     /* Rockport, Palmont installed by the Prep Tool */
    int sel, top, confirm, fonts;
    char status[96];    /* Reset caches result */
    char xstatus[128];  /* Save logs result */
    const char *busy;   /* a long job's text, drawn in the description line */
    float frac;         /* its progress, -1 = none */
} BsUi;
static BsFont g_bsf_title, g_bsf_label, g_bsf_small;
static const char *const k_bs_names[I_N] = { "Enable logging", "Save logs as ZIP", "Limit frame rate", "Fast travel", "Right stick look",
                                             "Ignore Visual Rating",
                                             "Enable trophies", "Full detail", "World Expansion", "Rockport and Palmont",
                                             "Reset caches", "Start game" };
static const char *const k_bs_help[I_N] = {
    "Save logs for bug reports.",
    "One ZIP in ux0:data/NFSU2 for bug reports. Left/Right: which sessions.",
    "Limit to 30 FPS. Turn off to allow up to 60 FPS.",
    "World Map: choose No on the GPS prompt to jump there.",
    "Aim the chase camera with the right stick. Down: look back.",
    "Career: DVD covers unlock without the star rating.",
    "Earn PS Vita trophies. Needs the NoTrpDrm plugin.",
    "Original reflections, traffic and particles. Lower frame rate.",
    "",
    "Drive Rockport and Palmont in Free Run. Needs the Prep Tool.",
    "Clear converted data and shaders. Your saves are kept.",
    "Start the game with these settings." };
/* layout in 720x408 units: the title bar, the rows full width (scrolled when
   they do not all fit), the description line, the button bar */
#define BS_S   ((float)BS_H / 408.0f)
#define BS_HB  (66 * BS_S)
#define BS_RX  (30 * BS_S)
#define BS_RY0 (BS_HB + 12 * BS_S)
#define BS_RH  (38 * BS_S)
#define BS_FB  (40 * BS_S)
static int bs_visible_rows(void)
{
    int v = (int)(((float)BS_H - BS_FB - 24 * BS_S - BS_RY0 + 5 * BS_S) / BS_RH);
    return v < 1 ? 1 : v > I_N ? I_N : v;
}

static void bs_draw(const BsUi *u)
{
    glViewport(0, 0, BS_W, BS_H);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (u->fonts) {
        const float S = BS_S, W = (float)BS_W, H = (float)BS_H;
        const float hb = BS_HB, rx = BS_RX, ry0 = BS_RY0, rh = BS_RH, fb = BS_FB;
        const BsFont *ft = &g_bsf_title, *fl = &g_bsf_label, *fs = &g_bsf_small;
        const float *white = BSC(255, 255, 255, 1.0f);
        const int vis = bs_visible_rows();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        /* Vita Settings green, with a soft light across the upper half. */
        bs_fill(0, 0, W, H, 0, BSC(112, 173, 18, 1.0f), BSC(29, 91, 9, 1.0f));
        bs_fill(0, 0, W, hb, 0, BSC(255, 255, 255, 0.08f), BSC(255, 255, 255, 0.0f));
        bs_text(ft, "Settings", rx + 10 * S, hb * 0.5f, white);
        const char *sub = "Need for Speed Underground 2";
        bs_text(fs, sub, W - rx - bs_text_w(fs, sub), hb * 0.5f, BSC(255, 255, 255, 0.8f));
        bs_fill(rx, hb, W - 2 * rx, 1 * S, 0, BSC(255, 255, 255, 0.45f), NULL);

        for (int vi = 0; vi < vis && u->top + vi < I_N; vi++) {
            int i = u->top + vi;
            float y = ry0 + vi * rh, h = rh - 5 * S, yc = y + h * 0.5f;
            if (BS_HEADER(i)) {   /* section title: text on the green, a thin rule under it */
                bs_text(fl, k_bs_names[i], rx + 6 * S, yc + 4 * S, white);
                bs_fill(rx, y + h, W - 2 * rx, 1 * S, 0, BSC(255, 255, 255, 0.45f), NULL);
                continue;
            }
            int on = i == u->sel;
            bs_fill(rx, y, W - 2 * rx, h, 6 * S,
                    on ? BSC(38, 145, 225, 1.0f) : BSC(18, 72, 4, 0.18f),
                    on ? BSC(8, 93, 184, 1.0f) : NULL);
            if (on && u->busy && u->frac >= 0.0f)   /* a long job fills its row */
                bs_fill(rx, y, (W - 2 * rx) * (u->frac > 1.0f ? 1.0f : u->frac), h, 6 * S,
                        BSC(255, 255, 255, 0.22f), NULL);
            bs_line(rx, y, W - 2 * rx, h, 6 * S, (on ? 2.0f : 1.0f) * S,
                    BSC(255, 255, 255, on ? 1.0f : 0.55f));
            bs_text(fl, k_bs_names[i], rx + 18 * S, yc, white);
            float vr = W - rx - 18 * S;
            if (i == I_LOGS || i == I_FPS || i == I_FTRAV || i == I_RSCAM || i == I_NOVR || i == I_TROPHY || i == I_FULL || i == I_XP) {
                int checked = i == I_LOGS ? u->logs : i == I_FTRAV ? u->ftrav : i == I_RSCAM ? u->rscam : i == I_NOVR ? u->novr :
                              i == I_TROPHY ? u->trophies :
                              i == I_FULL ? u->full : i == I_XP ? u->xp : !u->unlocked;
                float b = 24 * S, bx = vr - b, by = yc - b * 0.5f;
                bs_fill(bx, by, b, b, 2 * S, BSC(0, 0, 0, 0.12f), NULL);
                bs_line(bx, by, b, b, 2 * S, 1.5f * S, white);
                if (checked) bs_shape(bx + 3 * S, by + 3 * S, b - 6 * S, b - 6 * S,
                                      5.0f, 0, 2.8f * S, white, NULL);
                if (i == I_FPS) {
                    const char *v = u->unlocked ? "Up to 60 FPS" : "30 FPS";
                    bs_text(fs, v, bx - 16 * S - bs_text_w(fs, v), yc, white);
                }
                if (i == I_XP) {   /* which cities the Prep Tool installed */
                    const char *v = u->xp_city[0] && u->xp_city[1] ? "Both installed"
                                  : u->xp_city[0] ? "Rockport installed" : u->xp_city[1] ? "Palmont installed"
                                  : "Not installed";
                    bs_text(fs, v, bx - 16 * S - bs_text_w(fs, v), yc, white);
                }
            } else if (i == I_EXPORT) {
                const char *v = u->export_all ? "<  All sessions  >" : "<  Last session  >";
                bs_text(fs, v, vr - bs_text_w(fs, v), yc, white);
            } else {
                /* Small right-facing chevron, as in system setting rows. */
                bs_text(fl, ">", vr - bs_text_w(fl, ">"), yc, white);
            }
        }
        if (I_N > vis) {   /* scroll bar in the right margin */
            float tx = W - rx + 11 * S, tw = 4 * S, ty = ry0, th = vis * rh - 5 * S;
            float kh = th * (float)vis / (float)I_N, ky = ty + th * (float)u->top / (float)I_N;
            bs_fill(tx, ty, tw, th, 2 * S, BSC(255, 255, 255, 0.22f), NULL);
            bs_fill(tx, ky, tw, kh, 2 * S, BSC(255, 255, 255, 0.85f), NULL);
        }
        const char *d = u->busy ? u->busy
                      : u->sel == I_RESET && u->status[0] ? u->status
                      : u->sel == I_RESET && u->confirm ? "Select again to delete caches. Saves are kept."
                      : u->sel == I_EXPORT && u->xstatus[0] ? u->xstatus
                      : u->sel == I_XP && !u->xp_city[0] && !u->xp_city[1]
                        ? "Not installed: add NFS Most Wanted / Carbon with the PC Prep Tool."
                      : u->sel == I_XP && u->xp && !(u->xp_city[0] && u->xp_city[1])
                        ? (u->xp_city[0] ? "Rockport is ready. Add NFS Carbon in the Prep Tool for Palmont."
                                         : "Palmont is ready. Add NFS Most Wanted in the Prep Tool for Rockport.")
                      : k_bs_help[u->sel];
        float dy = ry0 + vis * rh + 12 * S;
        bs_text(fs, d, (W - bs_text_w(fs, d)) * 0.5f, dy, white);
        /* Quiet footer, using the same white symbols as system menus. */
        float fy = H - fb * 0.5f;
        bs_fill(0, H - fb, W, 1 * S, 0, BSC(255, 255, 255, 0.25f), NULL);
        float br = 9 * S, x = W - rx;
        const char *t2 = u->busy ? "Cancel" : "Start game", *t1 = "Enter";
        x -= bs_text_w(fs, t2); bs_text(fs, t2, x, fy, white);
        x -= 7 * S + br; bs_face_button(x, fy, br, 0); x -= br + 24 * S;
        if (!u->busy) {
            x -= bs_text_w(fs, t1); bs_text(fs, t1, x, fy, white);
            x -= 7 * S + br; bs_face_button(x, fy, br, 1);
        }
        bsr_flush();
        glDisable(GL_BLEND);
        glUseProgram(0);
    }
    bs_present();
}

/* the export's progress: redraws about every MB, Circle cancels */
typedef struct { BsUi *u; size_t base, total; char text[96]; } BsExportProg;
static int bs_export_prog(void *p, size_t done)
{
    BsExportProg *x = (BsExportProg *)p;
    float f = x->total ? (float)(x->base + done) / (float)x->total : 0.0f;
    snprintf(x->text, sizeof x->text, "Saving logs... %d%%", (int)(f * 100.0f + 0.5f));
    x->u->busy = x->text; x->u->frac = f;
    bs_draw(x->u);
    return (bs_pad() & STG_CIRCLE) != 0;
}
static void bs_export_logs(BsUi *u)
{
    static char runs[32][64];
    int nr = bs_list(BS_LOGS, 1, runs, 32), k = 0;
    for (int i = 0; i < nr; i++)   /* dated run folders only ("latest" is this run) */
        if (runs[i][0] >= '0' && runs[i][0] <= '9') memmove(runs[k++], runs[i], sizeof runs[0]);
    nr = k;
    if (!nr) { snprintf(u->xstatus, sizeof u->xstatus, "No saved sessions yet. Play first, then come back."); return; }
    qsort(runs, (size_t)nr, sizeof runs[0], bsz_cmp);
    int first = u->export_all ? 0 : nr - 1;

    char stamp[32], name[64], out[160], path[256];
    bs_stamp(stamp, sizeof stamp);
    snprintf(name, sizeof name, "NFSU2_logs_%s.zip", stamp);
    snprintf(out, sizeof out, "%s/%s", BS_ZIPDIR, name);
    static BszZip z;
    memset(&z, 0, sizeof z);
    z.f = bs_fopen(out, "wb");
    if (!z.f) { snprintf(u->xstatus, sizeof u->xstatus, "Could not create the ZIP in ux0:data/NFSU2."); return; }

    /* the members, and their total size for the progress */
    static char files[BSZ_MAXE][64]; static int frun[BSZ_MAXE];
    int nf = 0; size_t total = 0;
    for (int r = first; r < nr; r++) {
        static char fl[32][64];
        snprintf(path, sizeof path, "%s/%s", BS_LOGS, runs[r]);
        int n = bs_list(path, 0, fl, 32);
        qsort(fl, (size_t)n, sizeof fl[0], bsz_cmp);
        for (int i = 0; i < n && nf < BSZ_MAXE - 1; i++) {
            snprintf(path, sizeof path, "%s/%s/%s", BS_LOGS, runs[r], fl[i]);
            long sz = bsz_size(path);
            if (sz < 0) continue;
            memcpy(files[nf], fl[i], sizeof files[0]); frun[nf] = r; nf++;
            total += (size_t)sz;
        }
    }
    vita_mark("settings: saving logs, %d session(s), %d files, %u KB -> %s", nr - first, nf,
              (unsigned)(total >> 10), out);
    BsExportProg pr = { u, 0, total, "" };
    int ok = 1, cancelled = 0;
    for (int i = 0; i < nf && ok; i++) {
        snprintf(path, sizeof path, "%s/%s/%s", BS_LOGS, runs[frun[i]], files[i]);
        size_t n = 0;
        unsigned char *data = bsz_read(path, &n);
        if (!data) continue;   /* gone or too big for memory: skipped */
        char member[96];
        snprintf(member, sizeof member, "%s/%s", runs[frun[i]], files[i]);
        bsz_dostime(runs[frun[i]], &z.dtime, &z.ddate);
        int r = bsz_add(&z, member, data, n, bs_export_prog, &pr);
        free(data);
        if (r < 0) { ok = 0; cancelled = 1; } else if (!r) ok = 0;
        pr.base += n;
        if (ok && bs_export_prog(&pr, 0)) { ok = 0; cancelled = 1; }
    }
    if (ok) {   /* info.txt: what was saved and the Settings switches, nothing else */
        char info[640];
        int len = snprintf(info, sizeof info,
            "OpenUG2 log export, saved %s (Vita local time)\r\n"
            "Sessions (each folder is named by the time that session started):\r\n", stamp);
        for (int r = first; r < nr && len < (int)sizeof info - 80; r++)
            len += snprintf(info + len, sizeof info - (size_t)len, "  %s\r\n", runs[r]);
        len += snprintf(info + len, sizeof info - (size_t)len,
            "Settings now: logging %s, frame rate %s, fast travel %s, right stick look %s, ignore visual rating %s, trophies %s, full detail %s\r\n",
            u->logs ? "on" : "off", u->unlocked ? "up to 60" : "30", u->ftrav ? "on" : "off", u->rscam ? "on" : "off",
            u->novr ? "on" : "off",
            u->trophies ? "on" : "off", u->full ? "on" : "off");
        if (len > (int)sizeof info) len = (int)sizeof info;
        bsz_dostime(stamp, &z.dtime, &z.ddate);
        if (bsz_add(&z, "info.txt", (const unsigned char *)info, (size_t)len, NULL, NULL) <= 0) ok = 0;
    }
    if (ok) bsz_finish(&z);
    if (z.err) ok = 0;
    if (fclose(z.f) != 0) ok = 0;
    u->busy = NULL; u->frac = -1.0f;
    if (!ok) {
        bs_remove(out);
        snprintf(u->xstatus, sizeof u->xstatus, cancelled ? "Cancelled. No ZIP was saved."
                                                          : "Could not save the ZIP. Is the memory card full?");
        vita_mark("settings: log ZIP %s", cancelled ? "cancelled" : "failed");
        return;
    }
    unsigned kb = (z.off + 1023) >> 10;
    if (kb >= 1024) snprintf(u->xstatus, sizeof u->xstatus, "Saved %s (%u.%u MB)", name, kb / 1024, (kb % 1024) * 10 / 1024);
    else snprintf(u->xstatus, sizeof u->xstatus, "Saved %s (%u KB)", name, kb);
    vita_mark("settings: log ZIP saved, %d members, %u KB from %u KB", z.ne, kb, (unsigned)(total >> 10));
}

/* flips a switch; its file exists while the switch is on (on_means_file) or off */
static void bs_toggle(int *v, const char *file, int on_means_file)
{
    *v = !*v;
    if ((*v != 0) == (on_means_file != 0)) bs_touch(file); else bs_remove(file);
}

static void boot_settings_run(void) {
    static BsUi u;
    memset(&u, 0, sizeof u);
    u.logs = !bs_exists(BS_NOLOG); u.unlocked = bs_exists(BS_FPS60); u.ftrav = bs_exists(BS_FTRAV);
    u.rscam = bs_exists(BS_RSCAM);
    u.novr = bs_exists(BS_NOVR);
    u.trophies = bs_exists(BS_TRP); u.full = bs_exists(BS_FULL); u.frac = -1.0f;
    u.xp = bs_exists(BS_XP);
    /* installed = translated by the Prep Tool (.xp marker beside the world), or
       the raw files the Vita translates itself on the first drive (xpcity.c city_ok) */
    u.xp_city[0] = bs_exists("ux0:data/NFSU2/TRACKS/STREAML2RA.xp") ||
                   bs_exists("ux0:data/NFSU2/expansion/mw/TRACKS/STREAML2RA.BUN");
    u.xp_city[1] = bs_exists("ux0:data/NFSU2/TRACKS/STREAML5RA.xp") ||
                   bs_exists("ux0:data/NFSU2/expansion/carbon/TRACKS/STREAML5RA.BUN");
    int quit = 0;
    unsigned prev = bs_pad();   /* the L+R that opened the screen is not a press */
    vita_mark("settings: opened (logs %s, frame rate %s, fast travel %s)", u.logs ? "on" : "off",
              u.unlocked ? "unlocked" : "30", u.ftrav ? "on" : "off");

    const float S = BS_S;
    u.fonts = bsr_init() &&
              bsf_build(&g_bsf_title, 0, (int)(25 * S + 0.5f)) && bsf_build(&g_bsf_label, 0, (int)(21 * S + 0.5f)) &&
              bsf_build(&g_bsf_small, 0, (int)(14.5f * S + 0.5f));
    if (!u.fonts) printf("settings: drawing unavailable, keys still work\n");
    const int vis = bs_visible_rows();
    /* touch: a tap (press and release without moving) picks a row and presses
       it; dragging up/down scrolls the list */
    int t_down = 0, t_moved = 0, t_x = 0, t_y = 0, t_top = 0;

    while (!quit) {
        unsigned now = bs_pad(), hit = now & ~prev;
        prev = now;
        int tx, ty, act = 0, moved_sel = 0;
        int held = bs_touch_now(&tx, &ty);
        if (held && !t_down) { t_down = 1; t_moved = 0; t_x = tx; t_y = ty; t_top = u.top; }
        else if (held) {
            float dy = (float)(ty - t_y);
            if (fabsf(dy) > 10 * S) t_moved = 1;
            if (t_moved && I_N > vis) {
                int nt = t_top - (int)floorf(dy / BS_RH + 0.5f);
                u.top = nt < 0 ? 0 : nt > I_N - vis ? I_N - vis : nt;
            }
        } else if (t_down) {
            t_down = 0;
            for (int vi = 0; !t_moved && vi < vis && u.top + vi < I_N; vi++) {
                float y = BS_RY0 + vi * BS_RH;
                if (t_x >= BS_RX && t_x < BS_W - BS_RX && t_y >= y && t_y < y + BS_RH - 5 * S) {
                    int i = u.top + vi;
                    if (BS_HEADER(i)) continue;
                    if (u.sel != i) u.confirm = 0;
                    u.sel = i; act = 1;
                }
            }
        }
        if (hit & STG_UP)   { do u.sel = (u.sel + I_N - 1) % I_N; while (BS_HEADER(u.sel)); u.confirm = 0; moved_sel = 1; }
        if (hit & STG_DOWN) { do u.sel = (u.sel + 1) % I_N; while (BS_HEADER(u.sel)); u.confirm = 0; moved_sel = 1; }
        if (moved_sel) {   /* keep the focus row on screen */
            if (u.sel < u.top) u.top = u.sel;
            if (u.sel >= u.top + vis) u.top = u.sel - vis + 1;
        }
        if (hit & (STG_CIRCLE | STG_START)) quit = 1;
        if (act || (hit & (STG_CROSS | STG_LEFT | STG_RIGHT))) {
            int cross = act || (hit & STG_CROSS) != 0;
            switch (u.sel) {
            case I_LOGS:   bs_toggle(&u.logs, BS_NOLOG, 0); u.status[0] = 0; break;
            case I_FPS:    bs_toggle(&u.unlocked, BS_FPS60, 1); u.status[0] = 0; break;
            case I_FTRAV:  bs_toggle(&u.ftrav, BS_FTRAV, 1); u.status[0] = 0; break;
            case I_RSCAM:  bs_toggle(&u.rscam, BS_RSCAM, 1); u.status[0] = 0; break;
            case I_NOVR:   bs_toggle(&u.novr, BS_NOVR, 1); u.status[0] = 0; break;
            case I_TROPHY: bs_toggle(&u.trophies, BS_TRP, 1); u.status[0] = 0; break;
            case I_FULL:   bs_toggle(&u.full, BS_FULL, 1); u.status[0] = 0; break;
            case I_XP:     bs_toggle(&u.xp, BS_XP, 1); u.status[0] = 0; break;
            case I_EXPORT:
                if (!cross) { u.export_all = !u.export_all; u.xstatus[0] = 0; break; }
                u.xstatus[0] = 0; u.busy = "Saving logs..."; u.frac = 0.0f;
                bs_draw(&u);
                bs_export_logs(&u);
                prev = bs_pad();   /* the Circle that cancelled is not a press */
                break;
            case I_RESET:
                if (!cross) break;
                if (!u.confirm) { u.confirm = 1; u.status[0] = 0; break; }
                {   int n = bs_rmtree(BS_CACHE);
                    snprintf(u.status, sizeof u.status, "Deleted %d files", n);
                    vita_mark("settings: cache reset, %d files deleted", n); }
                u.confirm = 0;
                break;
            case I_START:
                if (cross) quit = 1;
                break;
            }
        }
        bs_draw(&u);
    }
    vita_mark("settings: closed (logs %s, frame rate %s, fast travel %s, trophies %s, full detail %s, World Expansion %s), restarting",
              u.logs ? "on" : "off", u.unlocked ? "unlocked" : "30", u.ftrav ? "on" : "off", u.trophies ? "on" : "off",
              u.full ? "on" : "off", u.xp ? "on" : "off");
}
