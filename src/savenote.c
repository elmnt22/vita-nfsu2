/* savenote.c - the "Save complete." note in the corner after a save.
 *
 * Not in the original (NFSU2 PC says nothing after an autosave): the user
 * asked for it (11-14) so a player knows the game saved, now that autosave
 * is on for new profiles. It is the game's own front-end font
 * (ConduitMdITC_TT21i from GLOBAL/GLOBALA.BUN) and the game's own
 * localized memory card line (memcard/LOCALE_<lang>.loc, realmc string 8),
 * drawn once into a small texture and blended over the finished frame. */
#include "savenote.h"
#include "render.h"
#include "feng.h"
#include "nfsu2.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *shop_rmc_string(const char *dataroot, int id);   /* shop.c */

#define SN_SHOW_MS 2600u
#define SN_FADE_IN_MS 200u
#define SN_FADE_OUT_MS 600u

static volatile Uint32 g_sn_at;     /* SDL_GetTicks of the last save, 0 = none */
static GLuint g_sn_tex, g_sn_prog, g_sn_vbo;
static GLint g_sn_alpha_loc = -1;
static int g_sn_w, g_sn_h, g_sn_failed;

void savenote_saved(void) {
    Uint32 t = SDL_GetTicks();
    g_sn_at = t ? t : 1;
}
void savenote_cancel(void) { g_sn_at = 0; }

/* the note's picture: a dark rounded panel, the line in the menus' green */
static int sn_build(const char *dataroot) {
    char path[600];
    long glen = 0;
    snprintf(path, sizeof path, "%s/GLOBAL/GLOBALA.BUN", dataroot);
    unsigned char *globa = n2_read_file(path, &glen);
    if (!globa) return 0;
    FengFont font;
    int ok = feng_load_font(globa, glen, "ConduitMdITC_TT21i", &font);
    FengImage img; memset(&img, 0, sizeof img);
    if (ok) { uint32_t key = font.atlas_key; feng_tpk_load(globa, glen, &key, 1, &img); }
    free(globa);
    if (!ok || !img.rgba) { printf("save note: no front-end font (%d)\n", ok); feng_image_free(&img); return 0; }

    const char *txt = shop_rmc_string(dataroot, 8);
    if (!txt || !txt[0]) txt = "Save complete.";
    int tw = (int)(feng_text_width(&font, txt) + 0.5f);
    int lh = font.line > 0 ? font.line : 24;
    int padx = 14, pady = 6;
    int w = tw + 2 * padx, h = lh + 2 * pady;
    if (w < 32 || w > 1024 || h > 128) { feng_image_free(&img); return 0; }
    unsigned char *px = (unsigned char *)calloc((size_t)w * (size_t)h, 4);
    if (!px) { feng_image_free(&img); return 0; }
    /* the panel: black at 60 %, corners rounded (radius 6) */
    const int rad = 6;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int dx = x < rad ? rad - x : x >= w - rad ? x - (w - rad - 1) : 0;
            int dy = y < rad ? rad - y : y >= h - rad ? y - (h - rad - 1) : 0;
            if (dx * dx + dy * dy > rad * rad) continue;
            unsigned char *p = px + ((size_t)y * w + x) * 4;
            p[0] = p[1] = p[2] = 0; p[3] = 150;
        }
    /* the glyphs: coverage from the atlas, a 1-pixel shadow, then the green
       (the menus' item text, 0xCDF07A-ish) */
    static const unsigned char col[3] = { 0xd2, 0xf0, 0x78 };
    for (int pass = 0; pass < 2; pass++) {
        float pen = (float)padx;
        int sx = pass == 0 ? 1 : 0, sy = pass == 0 ? 1 : 0;
        for (const unsigned char *c = (const unsigned char *)txt; *c; c++) {
            const FengGlyph *g = &font.g[*c];
            if (!g->ok) { pen += (float)lh * 0.3f; continue; }
            for (int gy = 0; gy < g->h; gy++)
                for (int gx = 0; gx < g->w; gx++) {
                    int ax = g->u + gx, ay = g->v + gy;
                    if (ax < 0 || ay < 0 || ax >= img.w || ay >= img.h) continue;
                    const unsigned char *a = img.rgba + ((size_t)ay * img.w + ax) * 4;
                    unsigned cov = a[3];
                    if (a[0] < cov) cov = (cov * a[0]) / 255u;   /* a white glyph on alpha, or a grey one */
                    if (!cov) continue;
                    int x = (int)pen + g->xo + gx + sx, y = pady + g->yo + gy + sy;
                    if (x < 0 || y < 0 || x >= w || y >= h) continue;
                    unsigned char *p = px + ((size_t)y * w + x) * 4;
                    float k = (float)cov / 255.0f;
                    for (int i = 0; i < 3; i++) {
                        float src = pass == 0 ? 0.0f : (float)col[i];
                        p[i] = (unsigned char)((float)p[i] * (1.0f - k) + src * k);
                    }
                    unsigned na = p[3] + (unsigned)((255 - p[3]) * k);
                    p[3] = (unsigned char)(na > 255 ? 255 : na);
                }
            pen += (float)g->adv;
        }
    }
    feng_image_free(&img);

    static const char *vs =
#ifdef N2_GLES
        "precision mediump float;\n"
#endif
        "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV;\n"
        "void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }\n";
    static const char *fs =
#ifdef N2_GLES
        "precision mediump float;\n"
#endif
        "varying vec2 vUV; uniform sampler2D uTex; uniform float uAlpha;\n"
        "void main(){ vec4 c=texture2D(uTex,vUV); gl_FragColor=vec4(c.rgb,c.a*uAlpha); }\n";
    GLuint p = glCreateProgram();
    const char *src[2] = { vs, fs };
    GLenum ty[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    for (int i = 0; i < 2; i++) {
        GLuint s = glCreateShader(ty[i]);
        glShaderSource(s, 1, &src[i], NULL); glCompileShader(s);
        glAttachShader(p, s);
        glDeleteShader(s);
    }
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUV");
    glLinkProgram(p);
    GLint linked = 0; glGetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked) { printf("save note: shader link failed\n"); glDeleteProgram(p); free(px); return 0; }
    g_sn_prog = p;
    g_sn_alpha_loc = glGetUniformLocation(p, "uAlpha");
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "uTex"), 0);

    glGenTextures(1, &g_sn_tex);
    glBindTexture(GL_TEXTURE_2D, g_sn_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(px);
    glGenBuffers(1, &g_sn_vbo);
    g_sn_w = w; g_sn_h = h;
    printf("save note: \"%s\" %dx%d\n", txt, w, h);
    return 1;
}

void savenote_draw(const char *dataroot, int sw, int sh) {
    Uint32 at = g_sn_at;
    if (!at || sw <= 0 || sh <= 0) return;
    Uint32 dt = SDL_GetTicks() - at;
    if (dt >= SN_SHOW_MS) { g_sn_at = 0; return; }
    if (!g_sn_tex) {
        if (g_sn_failed || !sn_build(dataroot)) { g_sn_failed = 1; g_sn_at = 0; return; }
    }
    float a = 1.0f;
    if (dt < SN_FADE_IN_MS) a = (float)dt / (float)SN_FADE_IN_MS;
    else if (dt > SN_SHOW_MS - SN_FADE_OUT_MS) a = (float)(SN_SHOW_MS - dt) / (float)SN_FADE_OUT_MS;

    /* bottom right (the menus' titles and the race HUD's position/lap are up
       top; the help bar ends short of this corner), 0.8 of its 640x480 size
       scaled to the screen height */
    float s = (float)sh / 480.0f * 0.8f;
    float w = (float)g_sn_w * s, h = (float)g_sn_h * s;
    float mx = 12.0f * s, my = 12.0f * s;
    float x1 = (float)sw - mx, x0 = x1 - w, y1 = (float)sh - my, y0 = y1 - h;   /* pixels from the top left */
    float X0 = x0 / (float)sw * 2.0f - 1.0f, X1 = x1 / (float)sw * 2.0f - 1.0f;
    float Y0 = 1.0f - y0 / (float)sh * 2.0f, Y1 = 1.0f - y1 / (float)sh * 2.0f;
    const float v[16] = { X0, Y1, 0.0f, 1.0f,   X1, Y1, 1.0f, 1.0f,
                          X0, Y0, 0.0f, 0.0f,   X1, Y0, 1.0f, 0.0f };

    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, sw, sh);
    glUseProgram(g_sn_prog);
    glUniform1f(g_sn_alpha_loc, a);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_sn_tex);
    glBindBuffer(GL_ARRAY_BUFFER, g_sn_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (const void *)8);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (!blend) glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
}
