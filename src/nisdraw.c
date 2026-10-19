/* nisdraw.c -- the pre-race scene's characters on screen; see nisdraw.h. */
#include "nisdraw.h"
#include "render.h"
#include "carfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* one slice (0x134b02 record) of a solid, its vertices made unique to it so
   each carries skeleton bone numbers through the slice's palette */
typedef struct {
    int nv, ni;
    float *v;                 /* nv x 11: pos3 nor3 uv2 weights3 */
    unsigned char *b;         /* nv x 3: skeleton bones (palette +0x21 [index]) */
    GLuint ibo;
    GLuint tex; int cut, blend;
    float dmin[4], drange[4]; /* the light material (0x134013 [+0x20]; 0xff = DEFAULT) */
} NdSub;
typedef struct { unsigned hash; NdSub *sub; int nsub; } NdSolid;

struct NisDraw {
    NdSolid *sol; int nsol;
    GLuint *texs; int ntex;
    GLuint vbo, shadow_tex, shadow_ibo;
    GLuint prog;
    GLint uMVP, uEye, uSH[10], uDMin, uDRange, uTex, uCut, uBlend, uDofW;
    float *scratch; int scratch_n;
};

static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static float rdf(const unsigned char *p) { unsigned u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

typedef struct { const unsigned char *p; unsigned len; } Leaf;
typedef struct { Leaf hdr, tex, mat, vtx, rec, idx; } SolidLeaves;

static void obj_leaves(const unsigned char *d, unsigned len, SolidLeaves *o)
{
    for (unsigned q = 0; q + 8 <= len;) {
        unsigned cid = rd32(d + q), sz = rd32(d + q + 4);
        if (q + 8 + sz > len) break;
        const unsigned char *c = d + q + 8;
        if (cid & 0x80000000u) obj_leaves(c, sz, o);
        else if (cid == 0x134011u && !o->hdr.p) { o->hdr.p = c; o->hdr.len = sz; }
        else if (cid == 0x134012u && !o->tex.p) { o->tex.p = c; o->tex.len = sz; }
        else if (cid == 0x134013u && !o->mat.p) { o->mat.p = c; o->mat.len = sz; }
        else if (cid == 0x134b01u && !o->vtx.p) { o->vtx.p = c; o->vtx.len = sz; }
        else if (cid == 0x134b02u && !o->rec.p) { o->rec.p = c; o->rec.len = sz; }
        else if (cid == 0x134b03u && !o->idx.p) { o->idx.p = c; o->idx.len = sz; }
        q += 8 + sz;
    }
}
/* the 0x11 filler before a leaf's data (vertices: single bytes, then the
   body must be whole 60-byte vertices; records and indices: 0x1111 words) */
static unsigned pad_bytes(const Leaf *l, unsigned unit, int words)
{
    unsigned p = 0;
    if (words) while (p + 2 <= l->len && l->p[p] == 0x11 && l->p[p + 1] == 0x11) p += 2;
    else while (p < l->len && l->p[p] == 0x11) p++;
    while (p > 0 && (l->len - p) % unit) p--;
    return (l->len - p) % unit ? l->len : p;
}

static GLuint tex_of(NisDraw *d, const unsigned char *b, long blen, N2Tpk *tk, unsigned key,
                     unsigned *keys, int *mode)
{
    for (int i = 0; i < d->ntex; i++) if (keys[i] == key) { *mode = (int)(keys[64 + i]); return d->texs[i]; }
    if (d->ntex >= 64) return 0;
    N2Tex t; memset(&t, 0, sizeof t);
    GLuint id = 0;
    int m = N2_DRAW_OPAQUE;
    if (n2_tpk_decode(b, blen, *tk, key, &t) && t.rgb) {
        id = upload_tex(&t);
        m = n2_tex_mode(&t);
    }
    free(t.rgb); free(t.alpha); free(t.dxt);
    d->texs[d->ntex] = id; keys[d->ntex] = key; keys[64 + d->ntex] = (unsigned)m;
    d->ntex++;
    *mode = m;
    return id;
}

static int load_solid(NisDraw *d, NdSolid *s, const SolidLeaves *L, const unsigned char *b, long blen,
                      N2Tpk *tk, unsigned *keys)
{
    if (!L->vtx.p || !L->rec.p || !L->idx.p) return 0;
    unsigned vp = pad_bytes(&L->vtx, 60, 0), rp = pad_bytes(&L->rec, 60, 1), ip = pad_bytes(&L->idx, 2, 1);
    if (vp >= L->vtx.len || rp >= L->rec.len || ip >= L->idx.len) return 0;
    int nv = (int)((L->vtx.len - vp) / 60), nr = (int)((L->rec.len - rp) / 60), nidx = (int)((L->idx.len - ip) / 2);
    const unsigned char *V = L->vtx.p + vp, *R = L->rec.p + rp, *I = L->idx.p + ip;
    s->sub = (NdSub *)calloc((size_t)nr, sizeof *s->sub);
    int *map = (int *)malloc(sizeof(int) * (size_t)nv);
    unsigned short *li = (unsigned short *)malloc(sizeof(unsigned short) * (size_t)nidx);
    if (!s->sub || !map || !li) { free(s->sub); s->sub = NULL; free(map); free(li); return 0; }
    for (int r = 0; r < nr; r++) {
        const unsigned char *rec = R + 60 * r;
        unsigned cnt = rd32(rec + 12), start = rd32(rec + 52);
        if (start + cnt > (unsigned)nidx || cnt < 3) continue;
        NdSub *u = &s->sub[s->nsub];
        for (int i = 0; i < nv; i++) map[i] = -1;
        u->v = (float *)malloc(sizeof(float) * 11 * cnt);
        u->b = (unsigned char *)malloc(3 * cnt);
        if (!u->v || !u->b) { free(u->v); free(u->b); memset(u, 0, sizeof *u); continue; }
        const unsigned char *pal = rec + 0x21;                       /* 0x5bdcd0 */
        int bad = 0;
        for (unsigned k = 0; k < cnt; k++) {
            unsigned vi = I[2 * (start + k)] | I[2 * (start + k) + 1] << 8;
            if (vi >= (unsigned)nv) { bad = 1; break; }
            if (map[vi] < 0) {
                const unsigned char *x = V + 60 * vi;
                float *o = u->v + 11 * u->nv;
                for (int j = 0; j < 6; j++) o[j] = rdf(x + 4 * j);    /* position, normal */
                o[6] = rdf(x + 28); o[7] = rdf(x + 32);               /* uv (+24 is a colour) */
                for (int j = 0; j < 3; j++) {
                    o[8 + j] = rdf(x + 36 + 4 * j);                   /* BLENDWEIGHT */
                    int pi = (int)rdf(x + 48 + 4 * j);                /* BLENDINDICES: palette slot */
                    u->b[3 * u->nv + j] = pal[pi < 0 ? 0 : pi > 15 ? 15 : pi];
                }
                map[vi] = u->nv++;
            }
            li[k] = (unsigned short)map[vi];
        }
        if (bad) { free(u->v); free(u->b); memset(u, 0, sizeof *u); continue; }
        u->ni = (int)cnt;
        glGenBuffers(1, &u->ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, u->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(cnt * 2), li, GL_STATIC_DRAW);
        /* texture: the 0x134012 slot list [+0x1c] (8-byte entries) */
        unsigned slot = rec[0x1c], key = slot * 8 + 4 <= L->tex.len && L->tex.p ? rd32(L->tex.p + slot * 8) : 0;
        int mode = N2_DRAW_OPAQUE;
        u->tex = key ? tex_of(d, b, blen, tk, key, keys, &mode) : 0;
        u->cut = mode == N2_DRAW_CUTOUT;
        u->blend = mode == N2_DRAW_BLEND;
        /* light material: 0x134013 [+0x20] (8-byte entries), 0xff -> 0x48ac50 DEFAULT */
        unsigned mi = rec[0x20];
        const CarFxMat *m = NULL;
        if (mi != 0xff && L->mat.p && mi * 8 + 4 <= L->mat.len) m = carfx_material(rd32(L->mat.p + mi * 8));
        if (!m) m = carfx_default();
        memcpy(u->dmin, m->diff_min, sizeof u->dmin);
        memcpy(u->drange, m->diff_range, sizeof u->drange);
        if (u->nv > d->scratch_n) d->scratch_n = u->nv;
        s->nsub++;
    }
    free(map); free(li);
    return s->nsub > 0;
}

static int find_solid(const unsigned char *d, long len, unsigned hash, SolidLeaves *L)
{
    for (long q = 0; q + 8 <= len;) {
        unsigned cid = rd32(d + q), sz = rd32(d + q + 4);
        if (q + 8 + (long)sz > len) break;
        const unsigned char *c = d + q + 8;
        if (cid == 0x80134010u) {
            memset(L, 0, sizeof *L);
            obj_leaves(c, sz, L);
            if (L->hdr.p && L->hdr.len >= 0x20 && rd32(L->hdr.p + 0x18) == hash) return 1;
        } else if ((cid & 0x80000000u) && find_solid(c, sz, hash, L)) return 1;
        q += 8 + sz;
    }
    return 0;
}

NisDraw *nisdraw_load(const Nis *n)
{
    if (!n || !nis_nchars(n)) return NULL;
    GLuint prog = render_bone_program();
    if (!prog) return NULL;
    long blen = 0;
    FILE *f = fopen(nis_bundle_path(n), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); blen = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = blen > 0 ? (unsigned char *)malloc((size_t)blen) : NULL;
    if (!b || fread(b, 1, (size_t)blen, f) != (size_t)blen) { fclose(f); free(b); return NULL; }
    fclose(f);
    NisDraw *d = (NisDraw *)calloc(1, sizeof *d);
    unsigned keys[128];                 /* [i] texture key, [64 + i] its draw mode */
    if (d) {
        d->texs = (GLuint *)calloc(64, sizeof(GLuint));
        d->sol = (NdSolid *)calloc((size_t)nis_nchars(n), sizeof *d->sol);
    }
    if (!d || !d->texs || !d->sol) { free(b); if (d) { free(d->texs); free(d->sol); free(d); } return NULL; }
    d->prog = prog;
    N2Tpk tk = n2_tpk_open(b, blen);
    /* every solid a character wears (0x61d050 -> 0x493600 by name hash) */
    for (int i = 0; i < nis_nchars(n); i++) {
        NisCharView v;
        if (!nis_char(n, i, &v)) continue;
        int have = 0;
        for (int k = 0; k < d->nsol; k++) if (d->sol[k].hash == v.solid) have = 1;
        if (have) continue;
        /* the 0x80134010 object whose 0x134011 +0x18 is the hash */
        SolidLeaves L;
        if (find_solid(b, blen, v.solid, &L)) {
            NdSolid *s = &d->sol[d->nsol];
            s->hash = v.solid;
            if (load_solid(d, s, &L, b, blen, &tk, keys)) d->nsol++;
        }
    }
    /* 0x4398a0: CHARACTERSHADOW (bStringHash) */
    int smode = 0;
    d->shadow_tex = tex_of(d, b, blen, &tk, nis_hash("CHARACTERSHADOW"), keys, &smode);
    free(tk.blk);
    free(b);
    /* a character's slices are skinned into one stream (one upload each) */
    d->scratch_n = 0;
    for (int k = 0; k < d->nsol; k++) {
        int nv = 0;
        for (int j = 0; j < d->sol[k].nsub; j++) nv += d->sol[k].sub[j].nv;
        if (nv > d->scratch_n) d->scratch_n = nv;
    }
    d->scratch = d->scratch_n ? (float *)malloc(sizeof(float) * 8 * (size_t)d->scratch_n) : NULL;
    glGenBuffers(1, &d->vbo);
    static const unsigned short fan[6] = { 0, 1, 2, 0, 2, 3 };    /* 0x5c5580: a 2-triangle fan */
    glGenBuffers(1, &d->shadow_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->shadow_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof fan, fan, GL_STATIC_DRAW);
    d->uMVP = glGetUniformLocation(prog, "uMVP");
    d->uEye = glGetUniformLocation(prog, "uEye");
    for (int i = 0; i < 10; i++) { char nm[8]; snprintf(nm, sizeof nm, "uSH%d", i); d->uSH[i] = glGetUniformLocation(prog, nm); }
    d->uDMin = glGetUniformLocation(prog, "uDMin");
    d->uDRange = glGetUniformLocation(prog, "uDRange");
    d->uTex = glGetUniformLocation(prog, "uTex");
    d->uCut = glGetUniformLocation(prog, "uCut");
    d->uBlend = glGetUniformLocation(prog, "uBlend");
    d->uDofW = glGetUniformLocation(prog, "uDofW");
    int nsub = 0, nvt = 0;
    for (int k = 0; k < d->nsol; k++) for (int j = 0; j < d->sol[k].nsub; j++) { nsub++; nvt += d->sol[k].sub[j].nv; }
    printf("nisdraw: %d solids (%d slices, %d vertices), %d textures, shadow %s\n",
           d->nsol, nsub, nvt, d->ntex, d->shadow_tex ? "ok" : "MISSING");
    if (!d->nsol || !d->scratch) { nisdraw_free(d); return NULL; }
    return d;
}

void nisdraw_free(NisDraw *d)
{
    if (!d) return;
    for (int k = 0; k < d->nsol; k++) {
        for (int j = 0; j < d->sol[k].nsub; j++) {
            NdSub *u = &d->sol[k].sub[j];
            free(u->v); free(u->b);
            if (u->ibo) glDeleteBuffers(1, &u->ibo);
        }
        free(d->sol[k].sub);
    }
    free(d->sol);
    for (int i = 0; i < d->ntex; i++) if (d->texs[i]) glDeleteTextures(1, &d->texs[i]);
    free(d->texs);
    if (d->vbo) glDeleteBuffers(1, &d->vbo);
    if (d->shadow_ibo) glDeleteBuffers(1, &d->shadow_ibo);
    free(d->scratch);
    free(d);
}

/* o = a * b, row vectors */
static void m4mul(const float *a, const float *b, float *o)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            o[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
}

static void bind_stream(NisDraw *d, const float *v, int n)
{
    glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)n * 8 * sizeof(float)), v, GL_DYNAMIC_DRAW);
}
/* the attributes at vertex `first` of the bound stream */
static void stream_at(int first)
{
    size_t o = (size_t)first * 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)o);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(o + 3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(o + 5 * sizeof(float)));
    glDisableVertexAttribArray(3);
}

int nisdraw_draw(NisDraw *d, const Nis *n, const float V[16], const float PV[16],
                 const void *lights, int nlights, int stride)
{
    if (!d || !n) return 0;
    /* the eye and the look direction from the view (column-major) */
    float eye[3], fwd[3] = { -V[2], -V[6], -V[10] };
    for (int c = 0; c < 3; c++) eye[c] = -(V[c * 4] * V[12] + V[c * 4 + 1] * V[13] + V[c * 4 + 2] * V[14]);
    glUseProgram(d->prog);
    glUniformMatrix4fv(d->uMVP, 1, GL_FALSE, PV);
    glUniform3f(d->uEye, eye[0], eye[1], eye[2]);
    glUniform1i(d->uTex, 0);
    glUniform1f(d->uBlend, 0.0f);
    glUniform1f(d->uDofW, render_dof_writes() ? 1.0f : 0.0f);   /* worldbone's depth pass */
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    int drawn = 0;
    float bw[64 * 16];
    unsigned char vis[64];
    int nch = nis_nchars(n) < 64 ? nis_nchars(n) : 64;
    for (int i = 0; i < nch; i++) {
        NisCharView c;
        float p[3];
        vis[i] = 0;
        /* 0x48acb0 / 0x48f6f0: off-screen characters are not drawn (here a
           1.5 m sphere about the pelvis against the frustum); culled first,
           so a hidden one never builds its skinning matrices */
        if (!nis_char_pelvis(n, i, p)) continue;
        float cx = PV[0] * p[0] + PV[4] * p[1] + PV[8] * p[2] + PV[12];
        float cy = PV[1] * p[0] + PV[5] * p[1] + PV[9] * p[2] + PV[13];
        float cw = PV[3] * p[0] + PV[7] * p[1] + PV[11] * p[2] + PV[15];
        float r = 1.5f * (fabsf(PV[0]) + fabsf(PV[5]));
        if (cw < -1.5f || cx < -cw - r || cx > cw + r || cy < -cw - r || cy > cw + r) continue;
        if (!nis_char(n, i, &c) || c.nbones > 64) continue;
        const NdSolid *s = NULL;
        for (int k = 0; k < d->nsol; k++) if (d->sol[k].hash == c.solid) s = &d->sol[k];
        if (!s) continue;
        vis[i] = 1;
        /* the light rig at the pelvis (0x61d150: bone 1's translation) */
        float sh[10][4];
        carfx_sh(sh, c.pelvis, fwd, lights, nlights, stride);
        for (int k = 0; k < 10; k++) glUniform4fv(d->uSH[k], 1, sh[k]);
        /* BlendMatrices x the world object: model -> world per bone */
        for (int k = 0; k < c.nbones; k++) m4mul(c.skin + 16 * k, c.W, bw + 16 * k);
        float *o = d->scratch;
        for (int j = 0; j < s->nsub; j++) {
            const NdSub *u = &s->sub[j];
            for (int k = 0; k < u->nv; k++, o += 8) {
                const float *x = u->v + 11 * k;
                const unsigned char *bb = u->b + 3 * k;
                float px = 0, py = 0, pz = 0;
                for (int w = 0; w < 3; w++) {
                    float wt = x[8 + w];
                    if (wt == 0.0f) continue;
                    const float *m = bw + 16 * (bb[w] < c.nbones ? bb[w] : 0);
                    px += wt * (x[0] * m[0] + x[1] * m[4] + x[2] * m[8] + m[12]);
                    py += wt * (x[0] * m[1] + x[1] * m[5] + x[2] * m[9] + m[13]);
                    pz += wt * (x[0] * m[2] + x[1] * m[6] + x[2] * m[10] + m[14]);
                }
                const float *m = bw + 16 * (bb[0] < c.nbones ? bb[0] : 0);   /* the normal: first bone only */
                o[0] = px; o[1] = py; o[2] = pz;
                o[3] = x[6]; o[4] = x[7];
                o[5] = x[3] * m[0] + x[4] * m[4] + x[5] * m[8];
                o[6] = x[3] * m[1] + x[4] * m[5] + x[5] * m[9];
                o[7] = x[3] * m[2] + x[4] * m[6] + x[5] * m[10];
            }
        }
        bind_stream(d, d->scratch, (int)((o - d->scratch) / 8));
        for (int j = 0, first = 0; j < s->nsub; first += s->sub[j].nv, j++) {
            const NdSub *u = &s->sub[j];
            glUniform4fv(d->uDMin, 1, u->dmin);
            glUniform4fv(d->uDRange, 1, u->drange);
            glUniform1f(d->uCut, u->cut ? 1.0f : 0.0f);
            glBindTexture(GL_TEXTURE_2D, u->tex);
            stream_at(first);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, u->ibo);
            glDrawElements(GL_TRIANGLES, u->ni, GL_UNSIGNED_SHORT, 0);
        }
        drawn++;
    }
    /* 0x433220: the shadows, colour 0xff808080 through the world shading
       (2 x tex x 0.5), blended by the texture's alpha, no depth writes */
    if (d->shadow_tex && drawn) {
        static const float half[4] = { 0.5f, 0.5f, 0.5f, 0.5f }, zero[4] = { 0, 0, 0, 0 }, one[4] = { 1, 1, 1, 1 };
        glUniform4fv(d->uSH[0], 1, half);
        for (int k = 1; k < 10; k++) glUniform4fv(d->uSH[k], 1, zero);
        glUniform4fv(d->uDMin, 1, one);
        glUniform4fv(d->uDRange, 1, zero);
        glUniform1f(d->uCut, 0.0f);
        glUniform1f(d->uBlend, 1.0f);
        glBindTexture(GL_TEXTURE_2D, d->shadow_tex);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int i = 0; i < nch; i++) {
            float q[4][3];
            if (!vis[i] || !nis_char_shadow(n, i, q)) continue;
            float v[4 * 8];
            for (int k = 0; k < 4; k++) {
                float *o = v + 8 * k;
                o[0] = q[k][0]; o[1] = q[k][1]; o[2] = q[k][2];
                o[3] = uv[k][0]; o[4] = uv[k][1];
                o[5] = 0; o[6] = 0; o[7] = 1;
            }
            bind_stream(d, v, 4);
            stream_at(0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->shadow_ibo);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return drawn;
}
