/* aciddraw.c -- draws the Acid particles as 0x48bdf0 does; see aciddraw.h.
 * Pass 0: every group's camera billboards (0x5c0e10 -> 0x5bb430), pass 1:
 * every group's plane quads (0x5c7e80). Effect worldnodepth.fx: colour =
 * 2 * texture * vertex colour, alpha = texture * vertex alpha, no fog; depth
 * test on, depth writes off (0x5b7de0 ZWRITEENABLE 0), RGB writes only
 * (0x5c57e0 COLORWRITEENABLE 7); blending from the texture's TPK record
 * (0x5bcc30: additive for blend 2, else source alpha). */
#include "aciddraw.h"
#include "acid.h"
#include "render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float p[3], uv[2]; unsigned char c[4]; } AVert;   /* 24 bytes, as 0x5bb430 */

static struct {
    GLuint prog, vbo, ibo;
    GLint uMVP, uTex;
    AVert *v; int cap;
    int ready;
    GLuint white;
} A;

static GLuint tex_from(const unsigned char *b, long len, N2Tpk *tk, uint32_t key, int *blend)
{
    N2Tex t; memset(&t, 0, sizeof t);
    GLuint id = 0;
    if (n2_tpk_decode(b, len, *tk, key, &t) && t.rgb) { id = upload_tex(&t); *blend = t.blend; }
    free(t.rgb); free(t.alpha); free(t.dxt);
    return id;
}

int aciddraw_init(const char *dataroot, const unsigned char *glob, long globlen)
{
    if (A.ready) return 1;
    A.prog = render_particle_program();
    if (!A.prog) { printf("particles: program link failed\n"); return 0; }
    A.uMVP = glGetUniformLocation(A.prog, "uMVP");
    A.uTex = glGetUniformLocation(A.prog, "uTex");
    glGenBuffers(1, &A.vbo);
    glGenBuffers(1, &A.ibo);
    /* index pattern for up to 1000 quads: 0 1 2, 0 2 3 */
    unsigned short *ix = (unsigned short *)malloc(sizeof *ix * 6 * 1000);
    if (ix) {
        for (int q = 0; q < 1000; q++) {
            unsigned short b = (unsigned short)(q * 4);
            unsigned short *o = ix + q * 6;
            o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, A.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof *ix * 6 * 1000, ix, GL_STATIC_DRAW);
        free(ix);
    }
    /* textures: by hash, InGameCommon first (most of them), then GLOBALB (0x4901d0) */
    for (int f = 0; f < 2; f++) {
        long len = 0; unsigned char *b = NULL;
        if (f == 0) {
            char path[1024]; snprintf(path, sizeof path, "%s/GLOBAL/InGameCommon.bun", dataroot);
            b = n2_read_file(path, &len);
        } else { b = (unsigned char *)glob; len = globlen; }
        if (!b) continue;
        N2Tpk tk = n2_tpk_open(b, len);
        for (int i = 0; i < acid_ndefs(); i++) {
            AcidDef *d = acid_def_at(i);
            if (d->gltex || !d->tex) continue;
            /* the same texture key is shared by many definitions */
            for (int j = 0; j < i && !d->gltex; j++)
                if (acid_def_at(j)->tex == d->tex && acid_def_at(j)->gltex) {
                    d->gltex = acid_def_at(j)->gltex; d->blend = acid_def_at(j)->blend;
                }
            if (!d->gltex) d->gltex = tex_from(b, len, &tk, d->tex, &d->blend);
        }
        free(tk.blk);
        if (f == 0) free(b);
    }
    int have = 0, want = 0;
    for (int i = 0; i < acid_ndefs(); i++) { want += acid_def_at(i)->tex != 0; have += acid_def_at(i)->gltex != 0; }
    printf("particles: %d emitter definitions, textures %d of %d\n", acid_ndefs(), have, want);
    A.ready = 1;
    return 1;
}

void aciddraw_free(void)
{
    if (!A.ready) return;
    for (int i = 0; i < acid_ndefs(); i++) {
        AcidDef *d = acid_def_at(i);
        if (d->gltex) {
            GLuint t = d->gltex;
            for (int j = i; j < acid_ndefs(); j++) if (acid_def_at(j)->gltex == t) acid_def_at(j)->gltex = 0;
            glDeleteTextures(1, &t);
        }
    }
    if (A.vbo) glDeleteBuffers(1, &A.vbo);
    if (A.ibo) glDeleteBuffers(1, &A.ibo);
    free(A.v);
    memset(&A, 0, sizeof A);
}

/* cubic 0x4881d0: out = t^3*r0 + t^2*r1 + t*r2 + r3 */
static void cubic(const float *K, float t, float o[4])
{
    float t2 = t * t, t3 = t * t2;
    for (int j = 0; j < 4; j++) o[j] = t3 * K[j] + t2 * K[4 + j] + t * K[8 + j] + K[12 + j];
}
static float clamp255(float c) { return c > 0 ? (c > 255.0f ? 255.0f : c) : 0.0f; }

static const float aspect[4][2] = { {1, 1}, {1, 0.5f}, {0.5f, 1}, {1.1097829e-38f, 1.1097813e-38f} };  /* 0x7f3944 */

static void flush(GLuint tex, int blend, int nq)
{
    if (!nq) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glBlendFunc(GL_SRC_ALPHA, blend == 2 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glBindBuffer(GL_ARRAY_BUFFER, A.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(AVert) * 4 * (size_t)nq), A.v, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AVert), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(AVert), (void *)12);
    glVertexAttribPointer(RENDER_PARTICLE_COLOR_LOC, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(AVert), (void *)20);
    glDrawElements(GL_TRIANGLES, nq * 6, GL_UNSIGNED_SHORT, 0);
}

int aciddraw_draw(const float V[16], const float PV[16], float proj, const AcidOwner *incar)
{
    if (!A.ready || !A.prog) return 0;
    AcidGroup *first = acid_group_first();
    if (!first) return 0;
    if (A.cap < 4 * 1000) {
        free(A.v); A.v = (AVert *)malloc(sizeof(AVert) * 4 * 1000);
        if (!A.v) { A.cap = 0; return 0; }
        A.cap = 4 * 1000;
    }
    /* the camera's right / up in world space and the eye depth row (0x5b7de0) */
    const float R[3] = { V[0], V[4], V[8] }, U[3] = { V[1], V[5], V[9] };
    glUseProgram(A.prog);
    glUniformMatrix4fv(A.uMVP, 1, GL_FALSE, PV);
    glUniform1i(A.uTex, 0);
    glActiveTexture(GL_TEXTURE0);
    /* the world's normals (2) are not in this effect; on the Vita 2 is the colour */
    glDisableVertexAttribArray(5 - RENDER_PARTICLE_COLOR_LOC);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, A.ibo);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    int drawn = 0;
    for (int pass = 0; pass < 2; pass++) {
        GLuint btex = 0; int bblend = 0, nq = 0;
        for (AcidGroup *g = first; g; g = acid_group_next(g)) {
            int cnt = 0;
            const AcidParticle *p = acid_group_list(g, pass, &cnt);
            if (!p) continue;
            const AcidDef *d = acid_group_def(g);
            if (!d->gltex) continue;
            /* the view's own car in a bumper/hood camera: no engine / exhaust /
               mirror effects, no front-axle / front-wheel ones (0x48c0bf) */
            if (incar && acid_group_owner(g) == incar) {
                int s = d->slot;
                if (s == 1 || s == 5 || s == 6 || s == 9 || s == 10 || s == 11 || s == 26) continue;
            }
            if (d->gltex != btex || d->blend != bblend || nq + cnt > 1000) {
                flush(btex, bblend, nq); drawn += nq; nq = 0;
                btex = d->gltex; bblend = d->blend;
            }
            float inv_life = 1.0f / d->life;
            const float *col = acid_group_col(g);
            int cl = d->cols_log2, rl = d->rows_log2;
            int cmask = ~(-1 << cl), rmask = ~(-1 << rl);
            float du = 1.0f / (float)(1 << cl), dv = 1.0f / (float)(1 << rl);
            float maxs = d->maxsize != 0.0f ? d->maxsize : 512.0f;      /* def +0x8c, 0 -> 512 */
            for (; p; p = acid_particle_next(g, pass, p)) {
                float t = 1.0f - (float)p->life * 0.0009765625f * inv_life;
                float c[4], sz[4];
                cubic(col, t, c);
                for (int j = 0; j < 4; j++) c[j] = clamp255(c[j]);
                cubic(d->size, t, sz);
                float depth = -(V[2] * p->pos[0] + V[6] * p->pos[1] + V[10] * p->pos[2] + V[14]);
                float size = sz[0];
                float scr = size > 0 && depth > 0 ? size / depth * proj : 0.0f;
                if (scr > maxs) size = depth / proj * maxs;
                int asp = p->typ >> 4;
                float hw = size * aspect[asp][0] * 0.5f, hh = size * aspect[asp][1] * 0.5f;
                float u0 = (float)(p->frame & cmask) * du;
                float v0 = (float)((p->frame >> cl) & rmask) * dv;
                float u1 = u0 + du, v1 = v0 + dv;
                unsigned char col[4] = { (unsigned char)(int)c[0], (unsigned char)(int)c[1],
                                         (unsigned char)(int)c[2], (unsigned char)(int)c[3] };
                AVert *o = A.v + 4 * nq;
                const float *P = p->pos;
                if (pass == 0) {
                    if (col[3] <= 0x14) continue;   /* 0x5bb430 drops alpha <= 20 */
                    float r[3] = { R[0] * hw, R[1] * hw, R[2] * hw }, u[3] = { U[0] * hh, U[1] * hh, U[2] * hh };
                    for (int j = 0; j < 3; j++) {
                        o[0].p[j] = P[j] + r[j] + u[j];
                        o[1].p[j] = P[j] - r[j] + u[j];
                        o[2].p[j] = P[j] - r[j] - u[j];
                        o[3].p[j] = P[j] + r[j] - u[j];
                    }
                    o[0].uv[0] = u1; o[0].uv[1] = v1;
                    o[1].uv[0] = u0; o[1].uv[1] = v1;
                    o[2].uv[0] = u0; o[2].uv[1] = v0;
                    o[3].uv[0] = u1; o[3].uv[1] = v0;
                } else {
                    int spin = (int)sz[1];
                    int rot = (p->rot & 1) ? p->rot - spin : p->rot + spin;
                    int o4 = p->typ & 0xf;
                    float r[3], u[3];
                    static const float ax[4][2][3] = {
                        { {0,0,0}, {0,0,0} }, { {1,0,0}, {0,1,0} }, { {1,0,0}, {0,0,1} }, { {0,1,0}, {0,0,1} } };
                    if (o4 == 0) { memcpy(r, R, sizeof r); memcpy(u, U, sizeof u); }
                    else { memcpy(r, ax[o4 & 3][0], sizeof r); memcpy(u, ax[o4 & 3][1], sizeof u); }
                    if ((short)rot != 0) {
                        float cs = sinf((float)(unsigned short)(rot + 0x4000) * (6.28318530718f / 65536.0f));
                        float sn = sinf((float)(unsigned short)rot * (6.28318530718f / 65536.0f));
                        if (o4 == 0) {                                   /* 0x5bac90: * Rz */
                            float r2[3] = { r[0] * cs - r[1] * sn, r[0] * sn + r[1] * cs, r[2] };
                            float u2[3] = { u[0] * cs - u[1] * sn, u[0] * sn + u[1] * cs, u[2] };
                            memcpy(r, r2, sizeof r); memcpy(u, u2, sizeof u);
                        } else if (o4 == 1) { r[0] = cs; r[1] = sn; r[2] = 0; u[0] = -sn; u[1] = cs; u[2] = 0; }
                        else if (o4 == 2) { r[0] = cs; r[1] = 0; r[2] = -sn; u[0] = sn; u[1] = 0; u[2] = cs; }
                        else if (o4 == 3) { r[0] = 0; r[1] = cs; r[2] = sn; u[0] = 0; u[1] = -sn; u[2] = cs; }
                    }
                    float a[3], b[3];
                    for (int j = 0; j < 3; j++) { a[j] = hw * r[j] - hh * u[j]; b[j] = hw * r[j] + hh * u[j]; }
                    for (int j = 0; j < 3; j++) {
                        o[0].p[j] = P[j] - a[j];
                        o[1].p[j] = P[j] - b[j];
                        o[2].p[j] = P[j] + a[j];
                        o[3].p[j] = P[j] + b[j];
                    }
                    o[0].uv[0] = u0; o[0].uv[1] = v0;
                    o[1].uv[0] = u1; o[1].uv[1] = v0;
                    o[2].uv[0] = u1; o[2].uv[1] = v1;
                    o[3].uv[0] = u0; o[3].uv[1] = v1;
                }
                for (int k = 0; k < 4; k++) memcpy(o[k].c, col, 4);
                nq++;
            }
        }
        flush(btex, bblend, nq); drawn += nq;
    }
    glDisableVertexAttribArray(RENDER_PARTICLE_COLOR_LOC);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return drawn;
}
