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
     Reset caches            deletes ux0:data/NFSU2/cache (converted world
                             tiles, shaders, shop/parts lists); profiles,
                             options and the game data are kept
   Leaving the screen restarts the app, so the game boots from a clean state.

   The includer provides:
     BS_W, BS_H             screen size
     bs_pad()               buttons held now, STG_* bits
     bs_tap(&x, &y)         1 on a new front-touch press (screen pixels)
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
#define BS_CACHE  "ux0:data/NFSU2/cache"

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

static void boot_settings_run(void) {
    enum { I_LOGS, I_FPS, I_RESET, I_START, I_N };
    int logs = !bs_exists(BS_NOLOG), unlocked = bs_exists(BS_FPS60);
    int sel = 0, confirm = 0, quit = 0;
    char status[64] = "";
    unsigned prev = bs_pad();   /* the L+R that opened the screen is not a press */
    vita_mark("settings: opened (logs %s, frame rate %s)", logs ? "on" : "off", unlocked ? "unlocked" : "30");

    const float S = (float)BS_H / 408.0f;   /* layout in 720x408 units */
    static BsFont f_title, f_label, f_small;
    int fonts = bsr_init() &&
                bsf_build(&f_title, 0, (int)(25 * S + 0.5f)) && bsf_build(&f_label, 0, (int)(21 * S + 0.5f)) &&
                bsf_build(&f_small, 0, (int)(14.5f * S + 0.5f));
    if (!fonts) printf("settings: drawing unavailable, keys still work\n");

    static const char *names[I_N] = { "Enable logging", "Limit frame rate", "Reset caches", "Start game" };
    static const char *help[I_N] = {
        "Save logs for bug reports.",
        "Limit to 30 FPS. Turn off to allow up to 60 FPS.",
        "Clear converted data and shaders. Your saves are kept.",
        "Start the game with these settings." };
    /* the title bar, then the rows full width, then the button bar */
    const float hb = 66 * S, rx = 30 * S, ry0 = hb + 12 * S, rh = 57 * S, fb = 40 * S;

    while (!quit) {
        unsigned now = bs_pad(), hit = now & ~prev;
        prev = now;
        int tx, ty, act = 0;
        if (bs_tap(&tx, &ty)) {   /* a tap on a row picks it and presses it */
            for (int i = 0; i < I_N; i++) {
                float y = ry0 + i * rh;
                if (tx >= rx && tx < BS_W - rx && ty >= y && ty < y + rh - 5 * S) {
                    if (sel != i) confirm = 0;
                    sel = i; act = 1;
                }
            }
        }
        if (hit & STG_UP)   { sel = (sel + I_N - 1) % I_N; confirm = 0; }
        if (hit & STG_DOWN) { sel = (sel + 1) % I_N; confirm = 0; }
        if (hit & (STG_CIRCLE | STG_START)) quit = 1;
        if (act || (hit & (STG_CROSS | STG_LEFT | STG_RIGHT))) {
            int cross = act || (hit & STG_CROSS) != 0;
            switch (sel) {
            case I_LOGS:
                logs = !logs;
                if (logs) bs_remove(BS_NOLOG); else bs_touch(BS_NOLOG);
                status[0] = 0;
                break;
            case I_FPS:
                unlocked = !unlocked;
                if (unlocked) bs_touch(BS_FPS60); else bs_remove(BS_FPS60);
                status[0] = 0;
                break;
            case I_RESET:
                if (!cross) break;
                if (!confirm) { confirm = 1; status[0] = 0; break; }
                {   int n = bs_rmtree(BS_CACHE);
                    snprintf(status, sizeof status, "Deleted %d files", n);
                    vita_mark("settings: cache reset, %d files deleted", n); }
                confirm = 0;
                break;
            case I_START:
                if (cross) quit = 1;
                break;
            }
        }

        glViewport(0, 0, BS_W, BS_H);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (fonts) {
            const float W = (float)BS_W, H = (float)BS_H;
            const float *white = BSC(255, 255, 255, 1.0f);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            /* Vita Settings green, with a soft light across the upper half. */
            bs_fill(0, 0, W, H, 0, BSC(112, 173, 18, 1.0f), BSC(29, 91, 9, 1.0f));
            bs_fill(0, 0, W, hb, 0, BSC(255, 255, 255, 0.08f), BSC(255, 255, 255, 0.0f));
            bs_text(&f_title, "Settings", rx + 10 * S, hb * 0.5f, white);
            const char *sub = "Need for Speed Underground 2";
            bs_text(&f_small, sub, W - rx - bs_text_w(&f_small, sub), hb * 0.5f, BSC(255, 255, 255, 0.8f));
            bs_fill(rx, hb, W - 2 * rx, 1 * S, 0, BSC(255, 255, 255, 0.45f), NULL);

            for (int i = 0; i < I_N; i++) {
                float y = ry0 + i * rh, h = rh - 5 * S, yc = y + h * 0.5f;
                int on = i == sel;
                bs_fill(rx, y, W - 2 * rx, h, 6 * S,
                        on ? BSC(38, 145, 225, 1.0f) : BSC(18, 72, 4, 0.18f),
                        on ? BSC(8, 93, 184, 1.0f) : NULL);
                bs_line(rx, y, W - 2 * rx, h, 6 * S, (on ? 2.0f : 1.0f) * S,
                        BSC(255, 255, 255, on ? 1.0f : 0.55f));
                bs_text(&f_label, names[i], rx + 18 * S, yc, white);
                float vr = W - rx - 18 * S;
                if (i == I_LOGS || i == I_FPS) {
                    int checked = i == I_LOGS ? logs : !unlocked;
                    float b = 24 * S, bx = vr - b, by = yc - b * 0.5f;
                    bs_fill(bx, by, b, b, 2 * S, BSC(0, 0, 0, 0.12f), NULL);
                    bs_line(bx, by, b, b, 2 * S, 1.5f * S, white);
                    if (checked) bs_shape(bx + 3 * S, by + 3 * S, b - 6 * S, b - 6 * S,
                                          5.0f, 0, 2.8f * S, white, NULL);
                    if (i == I_FPS) {
                        const char *v = unlocked ? "Up to 60 FPS" : "30 FPS";
                        bs_text(&f_small, v, bx - 16 * S - bs_text_w(&f_small, v), yc, white);
                    }
                } else {
                    /* Small right-facing chevron, as in system setting rows. */
                    bs_text(&f_label, ">", vr - bs_text_w(&f_label, ">"), yc, white);
                }
            }
            const char *d = sel == I_RESET && status[0] ? status
                          : sel == I_RESET && confirm ? "Select again to delete caches. Saves are kept."
                          : help[sel];
            float dy = ry0 + I_N * rh + 17 * S;
            bs_text(&f_small, d, (W - bs_text_w(&f_small, d)) * 0.5f, dy, white);
            /* Quiet footer, using the same white symbols as system menus. */
            float fy = H - fb * 0.5f;
            bs_fill(0, H - fb, W, 1 * S, 0, BSC(255, 255, 255, 0.25f), NULL);
            float br = 9 * S, x = W - rx;
            const char *t2 = "Start game", *t1 = "Enter";
            x -= bs_text_w(&f_small, t2); bs_text(&f_small, t2, x, fy, white);
            x -= 7 * S + br; bs_face_button(x, fy, br, 0); x -= br + 24 * S;
            x -= bs_text_w(&f_small, t1); bs_text(&f_small, t1, x, fy, white);
            x -= 7 * S + br; bs_face_button(x, fy, br, 1);
            bsr_flush();
            glDisable(GL_BLEND);
            glUseProgram(0);
        }
        bs_present();
    }
    vita_mark("settings: closed (logs %s, frame rate %s), restarting", logs ? "on" : "off", unlocked ? "unlocked" : "30");
}
