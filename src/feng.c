/* feng.c — FEng package / TPK art / FNTF font reader. See feng.h.
 *
 * Layout facts (verified on GLOBAL/InGameRace.bun HUD_SingleRace.fng and the
 * retail packs; tools/fng_dump.py renders the same data as a PNG):
 *   'FEn\xe7' container of 4-char chunks: PkHd, TypS, 'Res\xcc'{RsNm, RsRq},
 *   'Obj\xcc'{'FOb\xea'{ObjD, Scrp...}}. ObjD holds 2-char tags, u16 sizes:
 *   Ot type, OP {guid, name hash, flags, resource index}, PA parent guid,
 *   SA {u32 b,g,r,a; pivot[3]; pos[3]; quat[4]; size[3]; uv[4] for images},
 *   St UTF-16 text, Sj justification.
 */
#include "feng.h"
#include "nfsu2.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t feng_hash(const char *s) {
    uint32_t h = 0xffffffffu;
    while (*s) h = h * 33u + (unsigned char)*s++;
    return h;
}

/* FEString label hash ('SH'): the game draws the language string of the label over the
   authored placeholder -- UI_Status_Region's first label is authored in Danish ("Status for
   gennemf<o-slash>relse af region:", label 838feef5 = "Region Completion Status:"). Kept: labels not
   in the table or empty, $token$ placeholders (the port's pad prompts), and strings too long
   for the text field */
void feng_apply_labels(FengPackage *pk, const uint32_t *sh, const char *const *st, int n, int all)
{
    if (!pk || !sh || !st || n <= 0) return;
    /* another language: every label, as the game draws them (the authored text is English);
       English keeps the port's narrower pass below */
    int other = n2_lang() != 0;
    if (other) all = 1;
    for (int i = 0; i < pk->nobj; i++) {
        FengObj *o = &pk->obj[i];
        /* only placeholders authored in another language (a non-ASCII character, loaded as '?'):
           elsewhere the port's screens set their texts as the game does (a general switch to the
           labels changes ~50 authored texts, GAPS F2.6 note) */
        if (o->type != FENG_STRING || !o->label || o->text[0] == '$' || (!all && !strchr(o->text, '?'))) continue;
        int lo = 0, hi = n - 1;
        while (lo <= hi) {
            int m = (lo + hi) >> 1;
            if (sh[m] < o->label) lo = m + 1;
            else if (sh[m] > o->label) hi = m - 1;
            else {
                size_t len = st[m] ? strlen(st[m]) : 0;
                int ascii = 1;
                for (size_t k = 0; k < len; k++) if ((unsigned char)st[m][k] >= 128) ascii = 0;
                if ((ascii || other) && len && len < FENG_TEXT) memcpy(o->text, st[m], len + 1);
                break;
            }
        }
    }
}

uint32_t feng_hash_upper(const char *s) {
    uint32_t h = 0xffffffffu;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 0x20;
        h = h * 33u + c;
    }
    return h;
}

uint32_t feng_tex_key(const char *resname) {
    char up[64]; int n = 0;
    for (; resname[n] && resname[n] != '.' && n < 63; n++)
        up[n] = (char)toupper((unsigned char)resname[n]);
    up[n] = 0;
    return feng_hash(up);
}

static float f32(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }

/* lower-case, extension stripped: "Font_X.ffn" -> "font_x" */
static void base_lower(const char *in, char *out, int cap) {
    int n = 0;
    for (; in[n] && in[n] != '.' && n < cap - 1; n++)
        out[n] = (char)tolower((unsigned char)in[n]);
    out[n] = 0;
}

/* ------------------------------------------------------------- package */

static int same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

/* One 'Scrp' chunk (0x5515e0's tag loop, the tags our packages use). */
/* caps hit while parsing the current package: 1 scripts, 2 tracks, 4 keys */
static int g_feng_trunc;
static void parse_script(const unsigned char *d, long t, long end, FengPackage *p,
                         FengObj *ob) {
    FengScript *sc = NULL;
    FengTrack *tr = NULL;
    while (t + 4 <= end) {
        int ts = d[t + 2] | d[t + 3] << 8;
        const unsigned char *v = d + t + 4;
        if (t + 4 + ts > end) break;
        if (!memcmp(d + t, "Sh", 2) && ts >= 16) {
            if (p->nscr >= FENG_MAX_SCR) { g_feng_trunc |= 1; return; }
            sc = &p->scr[p->nscr++];
            memset(sc, 0, sizeof *sc);
            sc->hash = n2_u32(v); sc->len = (int)n2_u32(v + 4);
            sc->flags = n2_u32(v + 8);
            sc->trk0 = (short)p->ntrk;
            if (!ob->nscr) ob->scr0 = (short)(p->nscr - 1);
            ob->nscr++;
            tr = NULL;
        } else if (!memcmp(d + t, "Sc", 2) && ts >= 4 && sc) sc->chain = n2_u32(v);
        else if (!memcmp(d + t, "EV", 2) && ts >= 12 && sc) {
            /* FEEventList: ts/12 events {msg, target, time}, e.g. an Event
               Handler's FORWARD = aadd0187 to the package at 0, then
               0xe1fde1d1 (EXIT_COMPLETE) to the game (-1) at 300 ms
               (IssueScriptMessages GC 8013931c) */
            for (int q = 0; q + 12 <= ts && p->nev < FENG_MAX_EV; q += 12) {
                FengEvent *e = &p->ev[p->nev++];
                e->msg = n2_u32(v + q); e->target = (int32_t)n2_u32(v + q + 4); e->time = (int)n2_u32(v + q + 8);
                if (!sc->nev) sc->ev0 = (short)(p->nev - 1);
                sc->nev++;
            }
        }
        else if (!memcmp(d + t, "FI", 2) && ts >= 8 && sc) {
            if (p->ntrk >= FENG_MAX_TRK) { g_feng_trunc |= 2; tr = NULL; t += 4 + ts; continue; }
            tr = &p->trk[p->ntrk++];
            memset(tr, 0, sizeof *tr);
            tr->type = v[0]; tr->size = v[1]; tr->interp = v[2]; tr->flags = v[3];
            tr->len = (int)(n2_u32(v + 4) & 0xffffff);
            tr->key0 = -1;
            sc->ntrk++;
        } else if (!memcmp(d + t, "To", 2) && ts >= 1 && tr) tr->off = v[0];
        else if (!memcmp(d + t, "Kd", 2) && tr) {
            int kw = 1 + tr->size / 4, nk = ts / (kw * 4);
            if (nk > 0 && p->nkeyw + nk * kw <= FENG_MAX_KEYW) {
                tr->key0 = p->nkeyw;
                for (int k = 0; k < nk * kw; k++) p->keyw[p->nkeyw++] = (int32_t)n2_u32(v + 4 * k);
                tr->nkey = (short)(nk - 1);
            } else if (nk > 0) g_feng_trunc |= 4;
        }
        t += 4 + ts;
    }
}

static void parse_object(const unsigned char *d, long o, long end,
                         FengPackage *p) {
    if (p->nobj >= FENG_MAX_OBJ) return;
    FengObj *ob = &p->obj[p->nobj];
    memset(ob, 0, sizeof *ob);
    ob->res = -1; ob->parent = -1; ob->root = p->nobj;
    ob->rgba[0] = ob->rgba[1] = ob->rgba[2] = ob->rgba[3] = 1.0f;
    ob->size[0] = ob->size[1] = 1.0f;
    ob->uv[2] = ob->uv[3] = 1.0f;
    {   /* data defaults: white, unit size, identity quaternion, full uv */
        float one = 1.0f; int32_t w1; memcpy(&w1, &one, 4);
        ob->data[0] = ob->data[1] = ob->data[2] = ob->data[3] = 255;
        ob->data[13] = w1; ob->data[14] = ob->data[15] = ob->data[16] = w1;
        ob->data[19] = ob->data[20] = w1;
    }
    uint32_t parent_guid = 0; int has_parent = 0;
    /* FOb chunk: sub-chunks; ObjD carries the tag stream */
    while (o + 8 <= end) {
        uint32_t sz = n2_u32(d + o + 4);
        if ((unsigned long)sz > (unsigned long)(end - o - 8)) break;   /* no signed wrap (audit 11-17) */
        long body = o + 8, bend = body + (long)sz;
        if (!memcmp(d + o, "ObjD", 4)) {
            long t = body;
            while (t + 4 <= bend) {
                int ts = d[t + 2] | d[t + 3] << 8;
                const unsigned char *v = d + t + 4;
                if (t + 4 + ts > bend) break;
                if (!memcmp(d + t, "Ot", 2) && ts >= 4) ob->type = (int)n2_u32(v);
                else if (!memcmp(d + t, "OP", 2) && ts >= 16) {
                    ob->guid = n2_u32(v); ob->hash = n2_u32(v + 4);
                    ob->flags = n2_u32(v + 8); ob->res = (int)n2_u32(v + 12);
                } else if (!memcmp(d + t, "PA", 2) && ts >= 4) {
                    parent_guid = n2_u32(v); has_parent = 1;
                } else if (!memcmp(d + t, "SA", 2) && ts >= 68) {
                    ob->rgba[2] = (float)n2_u32(v)      / 255.0f;   /* stored b,g,r,a */
                    ob->rgba[1] = (float)n2_u32(v + 4)  / 255.0f;
                    ob->rgba[0] = (float)n2_u32(v + 8)  / 255.0f;
                    ob->rgba[3] = (float)n2_u32(v + 12) / 255.0f;
                    ob->pivot[0] = f32(v + 16); ob->pivot[1] = f32(v + 20);
                    ob->pos[0] = f32(v + 28); ob->pos[1] = f32(v + 32);
                    ob->pos[2] = f32(v + 36);
                    /* quaternion about the screen axis only */
                    ob->angle = 2.0f * atan2f(f32(v + 48), f32(v + 52));
                    ob->size[0] = f32(v + 56); ob->size[1] = f32(v + 60);
                    if (ts >= 84) for (int k = 0; k < 4; k++) ob->uv[k] = f32(v + 68 + 4*k);
                    for (int k = 0; k < FENG_DATAW && 4 * k + 4 <= ts; k++)
                        ob->data[k] = (int32_t)n2_u32(v + 4 * k);
                } else if (!memcmp(d + t, "St", 2)) {
                    int n = 0;   /* UTF-16LE -> ASCII ('?' outside it) */
                    for (int k = 0; k + 1 < ts && n < FENG_TEXT - 1; k += 2) {
                        unsigned c = v[k] | v[k + 1] << 8;
                        if (!c) break;
                        ob->text[n++] = c < 128 ? (char)c : '?';
                    }
                    ob->text[n] = 0;
                } else if (!memcmp(d + t, "Sj", 2) && ts >= 4) ob->just = n2_u32(v);
                else if (!memcmp(d + t, "Sl", 2) && ts >= 4) ob->lead = (int)n2_u32(v);
                else if (!memcmp(d + t, "Sw", 2) && ts >= 4) ob->wrapw = (int)n2_u32(v);
                else if (!memcmp(d + t, "SH", 2) && ts >= 4) ob->label = n2_u32(v);
                t += 4 + ts;
            }
        } else if (!memcmp(d + o, "Scrp", 4)) parse_script(d, body, bend, p, ob);
        else if (!memcmp(d + o, "MsgR", 4)) {
            uint32_t msg = 0; FengResp *r = NULL;
            for (long t = body; t + 4 <= bend; ) {
                int ts = d[t + 2] | d[t + 3] << 8;
                const unsigned char *v = d + t + 4;
                if (t + 4 + ts > bend) break;
                if (ts >= 4) {
                    if (!memcmp(d + t, "Mi", 2)) msg = n2_u32(v);
                    else if (!memcmp(d + t, "Ri", 2) && p->nrsp < FENG_MAX_RSP) {
                        r = &p->rsp[p->nrsp++];
                        r->msg = msg; r->cmd = n2_u32(v); r->arg = 0; r->obj = (short)p->nobj;
                    } else if (!memcmp(d + t, "Ru", 2) && r) r->arg = n2_u32(v);
                }
                t += 4 + ts;
            }
        }
        o = bend;
    }
    /* Parents are referenced by guid, and guids repeat across the package:
       resolve to the NEAREST EARLIER group carrying it. */
    if (has_parent)
        for (int j = p->nobj - 1; j >= 0; j--)
            if (p->obj[j].guid == parent_guid && p->obj[j].type == FENG_GROUP) {
                ob->parent = j; break;
            }
    p->nobj++;
}

static void resolve(FengPackage *p) {
    for (int i = 0; i < p->nobj; i++) {
        FengObj *ob = &p->obj[i];
        float x = ob->pos[0], y = ob->pos[1], z = ob->pos[2], a = ob->angle;
        int q = ob->parent, root = i;
        while (q >= 0) {
            const FengObj *pa = &p->obj[q];
            if (pa->angle != 0.0f) {      /* y-down: positive = clockwise */
                float c = cosf(pa->angle), s = sinf(pa->angle);
                float nx = x * c - y * s, ny = x * s + y * c;
                x = nx; y = ny;
            }
            x += pa->pos[0]; y += pa->pos[1]; z += pa->pos[2];
            a += pa->angle;
            root = q; q = pa->parent;
        }
        ob->ax = x; ob->ay = y; ob->az = z; ob->aangle = a; ob->root = root;
    }
}

static int parse_feng(const unsigned char *d, long len, FengPackage *p) {
    if (len < 16 || memcmp(d, "FEn\xe7", 4)) return 0;
    long end = 8 + (long)n2_u32(d + 4);
    if (end > len) end = len;
    const unsigned char *names = NULL; long nlen = 0;
    long o = 8;
    while (o + 8 <= end) {
        uint32_t sz = n2_u32(d + o + 4);
        if ((unsigned long)sz > (unsigned long)(end - o - 8)) break;   /* no signed wrap (audit 11-17) */
        long body = o + 8, bend = body + (long)sz;
        if (!memcmp(d + o, "PkHd", 4) && sz > 0x18) {
            /* six u32 header words, then the package name */
            int n = 0;
            while (n < 39 && body + 0x18 + n < bend && d[body + 0x18 + n]) {
                p->name[n] = (char)d[body + 0x18 + n]; n++;
            }
            p->name[n] = 0;
        } else if (!memcmp(d + o, "Res\xcc", 4)) {
            long r = body;
            while (r + 8 <= bend) {               /* RsNm first, RsRq after */
                uint32_t rs = n2_u32(d + r + 4);
                if ((unsigned long)rs > (unsigned long)(bend - r - 8)) break;   /* runs past its parent */
                if (!memcmp(d + r, "RsNm", 4)) { names = d + r + 8; nlen = rs; }
                else if (!memcmp(d + r, "RsRq", 4) && names) {
                    uint32_t cnt = n2_u32(d + r + 8);
                    for (uint32_t k = 0; k < cnt && p->nres < FENG_MAX_RES; k++) {
                        const unsigned char *e = d + r + 12 + k * 24;
                        if (e + 24 > d + r + 8 + rs) break;
                        uint32_t noff = n2_u32(e + 4);
                        FengRes *fr = &p->res[p->nres++];
                        fr->type = (int)n2_u32(e + 8);
                        int n = 0;
                        while (n < 39 && (long)(noff + n) < nlen && names[noff + n]) {
                            fr->name[n] = (char)names[noff + n]; n++;
                        }
                        fr->name[n] = 0;
                    }
                }
                r += 8 + rs;
            }
        } else if (!memcmp(d + o, "Targ", 4)) {
            for (long t = body; t + 4 <= bend; ) {
                int ts = d[t + 2] | d[t + 3] << 8;
                if (t + 4 + ts > bend) break;
                if (!memcmp(d + t, "Mt", 2) && ts >= 4)
                    for (int k = 4; k + 4 <= ts && p->ntg < FENG_MAX_TG; k += 4) {
                        p->tg[p->ntg].msg = n2_u32(d + t + 4);
                        p->tg[p->ntg++].guid = n2_u32(d + t + 4 + k);
                    }
                t += 4 + ts;
            }
        } else if (!memcmp(d + o, "Obj\xcc", 4)) {
            long r = body;
            while (r + 8 <= bend) {
                uint32_t rs = n2_u32(d + r + 4);
                if ((unsigned long)rs > (unsigned long)(bend - r - 8)) break;   /* runs past its parent */
                if (!memcmp(d + r, "FOb\xea", 4)) parse_object(d, r + 8, r + 8 + rs, p);
                r += 8 + rs;
            }
        }
        o = bend;
    }
    resolve(p);
    return p->nobj > 0;
}

/* 1 when the name parse_feng would give the package (its last PkHd's, walked
   the same way) is `name` */
static int parse_feng_name_is(const unsigned char *d, long len, const char *name) {
    if (len < 16 || memcmp(d, "FEn\xe7", 4)) return 0;
    long end = 8 + (long)n2_u32(d + 4);
    if (end > len) end = len;
    char nm[40]; nm[0] = 0;
    for (long o = 8; o + 8 <= end; ) {
        uint32_t sz = n2_u32(d + o + 4);
        if ((unsigned long)sz > (unsigned long)(end - o - 8)) break;
        long body = o + 8, bend = body + (long)sz;
        if (!memcmp(d + o, "PkHd", 4) && sz > 0x18) {
            int n = 0;
            while (n < 39 && body + 0x18 + n < bend && d[body + 0x18 + n]) { nm[n] = (char)d[body + 0x18 + n]; n++; }
            nm[n] = 0;
        }
        o = bend;
    }
    return same_name(nm, name);
}

void feng_free_package(FengPackage *p) {
    if (!p) return;
    free(p->scr); free(p->trk); free(p->keyw);
    free(p->ev); free(p->tg); free(p->rsp);
    p->scr = NULL; p->trk = NULL; p->keyw = NULL;
    p->ev = NULL; p->tg = NULL; p->rsp = NULL;
    p->nscr = p->ntrk = p->nkeyw = 0;
    p->nev = p->ntg = p->nrsp = 0;
}

/* The scripts parse into scratch tables at the caps; the package keeps
   copies of the size it uses (most packages use a few hundred entries,
   UI_QRModeOptions thousands). */
static int keep_tables(FengPackage *out) {
    FengScript *sc = (FengScript *)malloc((size_t)(out->nscr ? out->nscr : 1) * sizeof *sc);
    FengTrack *tr = (FengTrack *)malloc((size_t)(out->ntrk ? out->ntrk : 1) * sizeof *tr);
    int32_t *kw = (int32_t *)malloc((size_t)(out->nkeyw ? out->nkeyw : 1) * sizeof *kw);
    FengEvent *ev = (FengEvent *)malloc((size_t)(out->nev ? out->nev : 1) * sizeof *ev);
    FengTarg *tg = (FengTarg *)malloc((size_t)(out->ntg ? out->ntg : 1) * sizeof *tg);
    FengResp *rs = (FengResp *)malloc((size_t)(out->nrsp ? out->nrsp : 1) * sizeof *rs);
    if (!sc || !tr || !kw || !ev || !tg || !rs) {
        free(sc); free(tr); free(kw); free(ev); free(tg); free(rs); return 0;
    }
    memcpy(sc, out->scr, (size_t)out->nscr * sizeof *sc);
    memcpy(tr, out->trk, (size_t)out->ntrk * sizeof *tr);
    memcpy(kw, out->keyw, (size_t)out->nkeyw * sizeof *kw);
    memcpy(ev, out->ev, (size_t)out->nev * sizeof *ev);
    memcpy(tg, out->tg, (size_t)out->ntg * sizeof *tg);
    memcpy(rs, out->rsp, (size_t)out->nrsp * sizeof *rs);
    out->scr = sc; out->trk = tr; out->keyw = kw;
    out->ev = ev; out->tg = tg; out->rsp = rs;
    return 1;
}

int feng_load_package(const unsigned char *bun, long len, const char *name,
                      FengPackage *out) {
    FengScript *t_scr = (FengScript *)malloc(FENG_MAX_SCR * sizeof *t_scr);
    FengTrack *t_trk = (FengTrack *)malloc(FENG_MAX_TRK * sizeof *t_trk);
    int32_t *t_keyw = (int32_t *)malloc(FENG_MAX_KEYW * sizeof *t_keyw);
    FengEvent *t_ev = (FengEvent *)malloc(FENG_MAX_EV * sizeof *t_ev);
    FengTarg *t_tg = (FengTarg *)malloc(FENG_MAX_TG * sizeof *t_tg);
    FengResp *t_rsp = (FengResp *)malloc(FENG_MAX_RSP * sizeof *t_rsp);
    int found = 0;
    long o = 0;
    while (t_scr && t_trk && t_keyw && t_ev && t_tg && t_rsp && o + 8 <= len) {
        uint32_t id = n2_u32(bun + o), sz = n2_u32(bun + o + 4);
        if ((unsigned long)sz > (unsigned long)(len - o - 8)) break;   /* no signed wrap (audit 11-17) */
        long body = o + 8, bend = body + (long)sz;
        if (id == 0x00030203u || id == 0x00030210u) {
            const unsigned char *pk = NULL; long pklen = 0;
            unsigned char *unpacked = NULL;
            if (id == 0x00030210u) {
                for (long j = body; j + 16 <= bend; j++)
                    if (!memcmp(bun + j, "JDLZ", 4)) {
                        long ulen = (long)n2_u32(bun + j + 8);
                        unpacked = (unsigned char *)malloc((size_t)ulen);
                        if (unpacked)
                            pklen = n2_jdlz(bun + j, (int)(bend - j), unpacked, (int)ulen);
                        pk = unpacked;
                        break;
                    }
            } else { pk = bun + body; pklen = (long)sz; }
            long f = -1;
            for (long j = 0; pk && j + 4 <= pklen && j < 64; j++)
                if (!memcmp(pk + j, "FEn\xe7", 4)) { f = j; break; }
            int ok = 0;
            /* another package: passed over on its header's name alone, not cleared and parsed
               whole first (GitHub #31: the front end's 49 packages, ~1,200 parses an open) */
            if (f >= 0 && parse_feng_name_is(pk + f, pklen - f, name)) {
                memset(out, 0, sizeof *out);
                out->scr = t_scr; out->trk = t_trk; out->keyw = t_keyw;
                out->ev = t_ev; out->tg = t_tg; out->rsp = t_rsp;
                g_feng_trunc = 0;
                ok = parse_feng(pk + f, pklen - f, out);
                if (ok && !same_name(out->name, name)) ok = 0;
            }
            free(unpacked);
            if (ok) {
                /* cFEng::PackageWasLoaded's platform pass, the console one (GC 8014de24 ->
                   FEngHidePCObjects::Callback 8014ddc4): every object with flag 0x8 (the PC
                   mouse buttons / arrows / filter text) is FEngSetInvisible'd -- bit 0 |
                   0x400000, recursing into a group's children -- and loses 0x10000000. Game
                   code may show one again (EventEngageDialog::Setup's Button3). The PC pass
                   (0x51c860, flag 0x40 console prompts) is not applied: the Vita is a console. */
                static unsigned char pc_hid[FENG_MAX_OBJ];
                int nhid = 0;
                for (int i = 0; i < out->nobj; i++) {
                    FengObj *ob = &out->obj[i];
                    int own = (ob->flags & 8u) != 0;
                    int par = ob->parent >= 0 && ob->parent < i && pc_hid[ob->parent];
                    pc_hid[i] = (unsigned char)(own || par);
                    if (own) ob->flags &= ~0x10000000u;
                    if (own || par) { ob->flags |= 1u | 0x400000u; nhid++; }
                }
                if (nhid) printf("feng: %s platform pass hid %d PC objects (flag 0x8)\n", name, nhid);
                printf("feng: %s %d objects, %d scripts, %d tracks, %d key words%s\n", name, out->nobj,
                       out->nscr, out->ntrk, out->nkeyw, g_feng_trunc ? " -- TRUNCATED (FENG_MAX_* too small)" : "");
                found = keep_tables(out); break;
            }
        }
        o = bend;
    }
    if (!found) {
        out->scr = NULL; out->trk = NULL; out->keyw = NULL; out->nscr = out->ntrk = out->nkeyw = 0;
        out->ev = NULL; out->tg = NULL; out->rsp = NULL; out->nev = out->ntg = out->nrsp = 0;
    }
    free(t_scr); free(t_trk); free(t_keyw); free(t_ev); free(t_tg); free(t_rsp);
    return found;
}

int feng_find(const FengPackage *p, uint32_t hash) {
    for (int i = 0; i < p->nobj; i++) if (p->obj[i].hash == hash) return i;
    return -1;
}

int feng_inside(const FengPackage *p, int obj, int anc) {
    for (int q = obj; q >= 0; q = p->obj[q].parent) if (q == anc) return 1;
    return 0;
}

/* ------------------------------------------------------- script runtime */

static float wf(int32_t w) { float f; memcpy(&f, &w, 4); return f; }
static int32_t fw(float f) { int32_t w; memcpy(&w, &f, 4); return w; }

/* one key's value words: base + key (ints for int/colour tracks) */
static void key_put(const FengTrack *tr, const int32_t *base, const int32_t *k,
                    int32_t *out) {
    int n = tr->size / 4;
    for (int i = 0; i < n; i++)
        out[i] = (tr->type == FENG_TRK_INT || tr->type == FENG_TRK_COLOR)
               ? base[i] + k[i] : fw(wf(base[i]) + wf(k[i]));
}

/* 0x52bdc0 / 0x519d40: a track's value at time t into out */
static void track_eval(const FengAnim *a, const FengTrack *tr, int t, int32_t *out) {
    const int kw = 1 + tr->size / 4, n = tr->size / 4;
    const int32_t *base = a->keyw + tr->key0 + 1;
    if (tr->nkey <= 0) { memcpy(out, base, (size_t)n * 4); return; }
    const int32_t *k0 = a->keyw + tr->key0 + kw;          /* first delta key */
#define KEY(i) (k0 + (i) * kw)
    int mode = tr->flags & 0x7f;
    if (mode == 2 && t > tr->len) t = 2 * tr->len - t;  /* ping-pong */
    int b = 0;                                           /* 0x5052b0 */
    while (b + 1 < tr->nkey && KEY(b)[0] < t) b++;
    int ia = -1, ib = b;
    float f;
    if (tr->interp == 0) {                               /* step */
        if (b > 0 && KEY(b)[0] > t) b--;
        key_put(tr, base, KEY(b) + 1, out);
        return;
    }
    if (mode == 1) {                                     /* wrap */
        int tb = KEY(b)[0];
        if (tb < t) {                                    /* past the last key: on to the first */
            ia = b; ib = 0;
            float span = (float)(tr->len + KEY(0)[0] - tb);
            f = span <= 1e-5f ? 0.0f : (float)(t - tb) / span;
        } else if (tb == t) { ia = b; f = 0.0f; }
        else if (b == 0) {                               /* before the first: from the last */
            int last = tr->nkey - 1, tl = KEY(last)[0];
            ia = last;
            float span = (float)(tr->len - tl + tb);
            f = span <= 1e-5f ? 0.0f : (float)(tr->len - tl + t) / span;
        } else {
            ia = b - 1;
            float span = (float)(tb - KEY(ia)[0]);
            f = span <= 1e-5f ? 0.0f : (float)(t - KEY(ia)[0]) / span;
        }
    } else {                                             /* clamp */
        ia = b - 1;
        if (ia < 0) f = 1.0f;
        else if (t < KEY(b)[0]) {
            float span = (float)(KEY(b)[0] - KEY(ia)[0]);
            f = span <= 1e-5f ? 0.0f : (float)(t - KEY(ia)[0]) / span;
        } else f = 1.0f;
    }
    if (f == 0.0f && ia >= 0) { key_put(tr, base, KEY(ia) + 1, out); return; }
    if (f == 1.0f || ia < 0) { key_put(tr, base, KEY(ib) + 1, out); return; }
    const int32_t *ka = KEY(ia) + 1, *kb = KEY(ib) + 1;
    for (int i = 0; i < n; i++) {
        if (tr->type == FENG_TRK_INT || tr->type == FENG_TRK_COLOR)
            out[i] = base[i] + ka[i] + (int32_t)((float)(kb[i] - ka[i]) * f);   /* ftol */
        else
            out[i] = fw((wf(kb[i]) - wf(ka[i])) * f + wf(ka[i]) + wf(base[i]));
    }
#undef KEY
}

/* 0x541db0 -> 0x5354f0 for each track of the object's script */
static void script_eval(FengAnim *a, int obj) {
    const FengPackage *p = a->p;
    const FengScript *sc = &p->scr[a->cur[obj]];
    for (int k = 0; k < sc->ntrk; k++) {
        int ti = sc->trk0 + k;
        const FengTrack *tr = &p->trk[ti];
        if (a->trk_done[ti] || tr->key0 < 0) continue;
        if (tr->off + tr->size / 4 > FENG_DATAW) continue;
        if (tr->interp <= 3) track_eval(a, tr, a->t[obj], &a->data[obj][tr->off]);
        if (tr->nkey <= 0) a->trk_done[ti] = 1;
    }
}

static int find_script(const FengPackage *p, int obj, uint32_t hash) {
    const FengObj *o = &p->obj[obj];
    for (int s = 0; s < o->nscr; s++)
        if (p->scr[o->scr0 + s].hash == hash) return o->scr0 + s;
    return -1;
}

/* 0x517720: a (re)started script evaluates all its tracks again */
static void script_arm(FengAnim *a, int s) {
    const FengScript *sc = &a->p->scr[s];
    for (int k = 0; k < sc->ntrk; k++) a->trk_done[sc->trk0 + k] = 0;
}

void feng_anim_init(FengAnim *a, const FengPackage *p) {
    memset(a, 0, sizeof *a);
    a->p = p;
    memcpy(a->keyw, p->keyw, (size_t)p->nkeyw * sizeof *a->keyw);
    for (int i = 0; i < p->nobj; i++) {
        memcpy(a->data[i], p->obj[i].data, sizeof a->data[i]);
        /* bit 0 is the only hide bit (FEngIsVisible 0x50c9e0, cFEng::
           RenderObject 0x51c670 / GC 8014d928). Bit 31 is tested only by
           RenderGroup (0x52c5d0 / GC 80147728) while FEngine+0 == 0, and the
           cFEng ctor (0x5372e0 / GC 8014d468 -> SetExecution 80145958) sets
           it to 1 for good: bit 31 never hides in the game. */
        a->hidden[i] = (p->obj[i].flags & 1u) != 0;
        a->cur[i] = (short)find_script(p, i, FENG_SCR_INIT);
        a->t[i] = 0;
        if (a->cur[i] >= 0) script_eval(a, i);
    }
}

int feng_anim_set(FengAnim *a, int obj, uint32_t hash, int restart) {
    if (obj < 0 || obj >= a->p->nobj) return 0;
    int s = find_script(a->p, obj, hash);
    if (s < 0) return 0;
    if (s != a->cur[obj]) { a->cur[obj] = (short)s; script_arm(a, s); a->t[obj] = 0; }
    if (restart) a->t[obj] = 0;
    return 1;
}

void feng_anim_eval_at(FengAnim *a, int obj, uint32_t hash, int t_ms) {
    if (obj < 0 || obj >= a->p->nobj) return;
    int s = find_script(a->p, obj, hash);
    if (s < 0) return;
    memcpy(a->data[obj], a->p->obj[obj].data, sizeof a->data[obj]);
    a->cur[obj] = (short)s;
    script_arm(a, s);
    a->t[obj] = t_ms;
    script_eval(a, obj);
}

int feng_anim_running(const FengAnim *a, int obj, uint32_t hash) {
    if (obj < 0 || obj >= a->p->nobj || a->cur[obj] < 0) return 0;
    const FengScript *sc = &a->p->scr[a->cur[obj]];
    return sc->hash == hash && a->t[obj] <= sc->len;
}

void feng_anim_hide(FengAnim *a, int obj, int hide) {
    if (obj < 0 || obj >= a->p->nobj) return;
    a->hidden[obj] = (unsigned char)(hide != 0);
    if (a->p->obj[obj].type == FENG_GROUP)
        for (int i = obj + 1; i < a->p->nobj; i++)
            if (a->p->obj[i].parent == obj) feng_anim_hide(a, i, hide);
}

void feng_anim_color(FengAnim *a, int obj, uint32_t argb) {
    if (obj < 0 || obj >= a->p->nobj) return;
    int32_t c[4] = { (int32_t)(argb & 0xff), (int32_t)(argb >> 8 & 0xff),
                     (int32_t)(argb >> 16 & 0xff), (int32_t)(argb >> 24) };
    const FengPackage *p = a->p;
    const FengObj *o = &p->obj[obj];
    for (int s = 0; s < o->nscr; s++) {                 /* 0x517ad0: every script's base */
        const FengScript *sc = &p->scr[o->scr0 + s];
        for (int k = 0; k < sc->ntrk; k++) {
            const FengTrack *tr = &p->trk[sc->trk0 + k];
            if (tr->off != 0 || tr->type != FENG_TRK_COLOR || tr->key0 < 0) continue;
            memcpy(a->keyw + tr->key0 + 1, c, sizeof c);
            a->trk_done[sc->trk0 + k] = 0;
            break;
        }
    }
    memcpy(a->data[obj], c, sizeof c);
}

void feng_anim_pos(FengAnim *a, int obj, float x, float y) {
    if (obj < 0 || obj >= a->p->nobj) return;
    const FengPackage *p = a->p;
    const FengObj *o = &p->obj[obj];
    int32_t v[3] = { fw(x), fw(y), a->data[obj][9] };
    for (int s = 0; s < o->nscr; s++) {                 /* 0x5178d0 field 2: every script's base */
        const FengScript *sc = &p->scr[o->scr0 + s];
        for (int k = 0; k < sc->ntrk; k++) {
            const FengTrack *tr = &p->trk[sc->trk0 + k];
            if (tr->off != 7 || tr->key0 < 0) continue;
            int n = tr->size / 4 < 3 ? tr->size / 4 : 3;
            memcpy(a->keyw + tr->key0 + 1, v, (size_t)n * 4);
            a->trk_done[sc->trk0 + k] = 0;
            break;
        }
    }
    memcpy(&a->data[obj][7], v, sizeof v);
}

/* words [off, off+n) of the object data and every script's base key of the
   track at that offset (0x5178d0 / 0x517c80) */
static void anim_field(FengAnim *a, int obj, int off, const int32_t *v, int n) {
    if (obj < 0 || obj >= a->p->nobj) return;
    const FengPackage *p = a->p;
    const FengObj *o = &p->obj[obj];
    for (int s = 0; s < o->nscr; s++) {
        const FengScript *sc = &p->scr[o->scr0 + s];
        for (int k = 0; k < sc->ntrk; k++) {
            const FengTrack *tr = &p->trk[sc->trk0 + k];
            if (tr->off != off || tr->key0 < 0) continue;
            int m = tr->size / 4 < n ? tr->size / 4 : n;
            memcpy(a->keyw + tr->key0 + 1, v, (size_t)m * 4);
            a->trk_done[sc->trk0 + k] = 0;
            break;
        }
    }
    memcpy(&a->data[obj][off], v, (size_t)n * 4);
}

void feng_anim_rot(FengAnim *a, int obj, float deg) {
    float r = deg * 0.0174533f;                         /* 0x51ced0: about the screen axis */
    int32_t q[4] = { 0, 0, fw(sinf(0.5f * r)), fw(cosf(0.5f * r)) };
    anim_field(a, obj, 10, q, 4);
}

void feng_anim_uv(FengAnim *a, int obj, float u0, float v0, float u1, float v1) {
    int32_t v[4] = { fw(u0), fw(v0), fw(u1), fw(v1) };
    anim_field(a, obj, 17, v, 4);
}

void feng_anim_size(FengAnim *a, int obj, float x, float y) {
    if (obj < 0 || obj >= a->p->nobj) return;
    int32_t v[3] = { fw(x), fw(y), a->data[obj][16] };
    anim_field(a, obj, 14, v, 3);
}

/* msg to one object: its responses (command 0 = run script arg on itself) */
static void msg_obj(FengAnim *a, int obj, uint32_t msg) {
    const FengPackage *p = a->p;
    for (int r = 0; r < p->nrsp; r++)
        if (p->rsp[r].obj == obj && p->rsp[r].msg == msg && p->rsp[r].cmd == 0)
            feng_anim_set(a, obj, p->rsp[r].arg, 1);
}
int feng_anim_msg(FengAnim *a, int obj, uint32_t msg) {   /* 0x51cdc0: FEng message to one object */
    int n = 0;
    const FengPackage *p = a->p;
    if (obj < 0 || obj >= p->nobj) return 0;
    for (int r = 0; r < p->nrsp; r++)
        if (p->rsp[r].obj == obj && p->rsp[r].msg == msg && p->rsp[r].cmd == 0) n++;
    msg_obj(a, obj, msg);
    return n;
}
int feng_anim_game_msg(FengAnim *a, uint32_t msg) {
    for (int i = 0; a && i < a->ngame; i++)
        if (a->game_msg[i] == msg) {
            memmove(&a->game_msg[i], &a->game_msg[i + 1], (size_t)(a->ngame - i - 1) * sizeof a->game_msg[0]);
            a->ngame--;
            return 1;
        }
    return 0;
}
static void msg_send(FengAnim *a, const FengEvent *e) {
    const FengPackage *p = a->p;
    if (e->target == -1) {                              /* SendMessageToGame: the owner polls it */
        if (a->ngame < (int)(sizeof a->game_msg / sizeof a->game_msg[0])) a->game_msg[a->ngame++] = e->msg;
    } else if (e->msg == 0x1b3909aau) {
        /* SetCurrentButton: not modelled here */
    } else if (e->target == -4 || e->target == 0) {     /* the package's Targ list (0: every package's;
                                                           only this one is known here) */
        for (int k = 0; k < p->ntg; k++) {
            if (p->tg[k].msg != e->msg) continue;
            for (int i = 0; i < p->nobj; i++)
                if (p->obj[i].guid == p->tg[k].guid) msg_obj(a, i, e->msg);
        }
    } else {
        for (int i = 0; i < p->nobj; i++)
            if (p->obj[i].guid == (uint32_t)e->target) msg_obj(a, i, e->msg);
    }
}

void feng_anim_tick(FengAnim *a, int dt_ms) {
    const FengPackage *p = a->p;
    for (int i = 0; i < p->nobj; i++) {                 /* 0x54ce60 */
        int s = a->cur[i];
        if (s < 0) continue;
        const FengScript *sc = &p->scr[s];
        int old = a->t[i], t = old + dt_ms;
        if (t < 0) t = 0;
        a->t[i] = t;
        if (sc->nev && old <= sc->len) {                /* the events the clock passed */
            for (int k = 0; k < sc->nev; k++) {
                const FengEvent *e = &p->ev[sc->ev0 + k];
                if (e->time >= old && (e->time < t || (e->time == sc->len && t >= sc->len)))
                    msg_send(a, e);
            }
            if (a->cur[i] != s) continue;               /* a response switched this object */
        }
        if (t >= sc->len) {
            if (!sc->chain) {
                int mode = (int)(sc->flags & 3);
                if (mode == 0) { a->t[i] = sc->len + 1; if (old == sc->len + 1) continue; }
                else if (mode == 1) {
                    if (sc->len > 0) { a->t[i] = t % sc->len; script_arm(a, s); }
                    else a->t[i] = 0;
                } else if (mode == 2) a->t[i] = sc->len > 0 ? t % (2 * sc->len) : 0;
            } else {
                script_eval(a, i);                       /* the end of this one */
                int over = t - sc->len;
                int n = find_script(p, i, sc->chain);
                a->cur[i] = (short)n;
                if (n < 0) continue;
                script_arm(a, n);
                a->t[i] = over;
            }
        }
        script_eval(a, i);
    }
}

void feng_anim_place(const FengAnim *a, int obj, FengPlace *out) {
    const FengPackage *p = a->p;
    const int32_t *d = a->data[obj];
    /* own transform: centre = pos + pivot - R * pivot (0x51b0b0 with v = 0) */
    float ang = 2.0f * atan2f(wf(d[12]), wf(d[13]));
    float c = cosf(ang), s = sinf(ang);
    float px = wf(d[4]), py = wf(d[5]);
    float x = wf(d[7]) + px - (px * c - py * s), y = wf(d[8]) + py - (px * s + py * c);
    int col[4] = { d[2], d[1], d[0], d[3] };            /* r g b a, 0..255 */
    int hidden = a->hidden[obj];
    out->sx = wf(d[14]); out->sy = wf(d[15]);
    for (int q = p->obj[obj].parent; q >= 0; q = p->obj[q].parent) {
        const int32_t *g = a->data[q];
        float ga = 2.0f * atan2f(wf(g[12]), wf(g[13]));  /* 0x52c5d0: R (v - piv) + pos + piv */
        float gc = cosf(ga), gs = sinf(ga), gpx = wf(g[4]), gpy = wf(g[5]);
        float vx = x - gpx, vy = y - gpy;
        x = vx * gc - vy * gs + wf(g[7]) + gpx;
        y = vx * gs + vy * gc + wf(g[8]) + gpy;
        ang += ga;
        int gcol[4] = { g[2], g[1], g[0], g[3] };
        for (int k = 0; k < 4; k++) col[k] = (gcol[k] * col[k] + 0x80) >> 8;
        /* RenderGroup 0x52c5d0 skips a group's subtree only on its alpha
           word 0; a group's own bit 0 is not tested (FEngSetInvisible
           0x50ca00 sets it on the children too) */
        if (g[3] == 0) hidden = 1;
    }
    for (int k = 0; k < 4; k++) {
        int v = col[k] < 0 ? 0 : col[k] > 255 ? 255 : col[k];
        out->rgba[k] = (float)v / 255.0f;
    }
    out->x = x; out->y = y; out->angle = ang; out->hidden = hidden;
}

/* ----------------------------------------------------------------- TPK */

void feng_image_free(FengImage *img) {
    if (!img) return;
    free(img->rgba); img->rgba = NULL; img->w = img->h = 0;
}

/* Decode one 0x7c-byte texture record. `rec` points at its NAME (+0x0c into
 * the row); offsets are relative to `pix`, which holds `plen` bytes. */
static int decode_record(const unsigned char *rec, const unsigned char *pix,
                         long plen, FengImage *out) {
    uint32_t off = n2_u32(rec + 0x24), paloff = n2_u32(rec + 0x28);
    uint32_t palsz = n2_u32(rec + 0x30);
    int w = rec[0x38] | rec[0x39] << 8, h = rec[0x3a] | rec[0x3b] << 8;
    unsigned char fmt = rec[0x3e];
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return 0;
    long n = (long)w * h, bx = (w + 3) / 4, by = (h + 3) / 4;
    unsigned char *rgba = (unsigned char *)malloc((size_t)n * 4);
    if (!rgba) return 0;
    int ok = 0;
    if (fmt == 0x08 && palsz >= 1024) {                       /* P8, RGBA palette */
        if ((long)paloff + 1024 <= plen && (long)off + n <= plen) {
            for (long i = 0; i < n; i++)
                memcpy(rgba + i * 4, pix + paloff + (long)pix[off + i] * 4, 4);
            ok = 1;
        }
    } else if (fmt == 0x20) {                                 /* BGRA8 */
        if ((long)off + n * 4 <= plen) {
            for (long i = 0; i < n; i++) {
                const unsigned char *s = pix + off + i * 4;
                rgba[i*4] = s[2]; rgba[i*4+1] = s[1]; rgba[i*4+2] = s[0]; rgba[i*4+3] = s[3];
            }
            ok = 1;
        }
    } else if (fmt == 0x22 || fmt == 0x24 || fmt == 0x26) {   /* DXT1/3/5 */
        long need = bx * by * (fmt == 0x22 ? 8 : 16);
        unsigned char *rgb = (unsigned char *)malloc((size_t)n * 3);
        unsigned char *alf = (unsigned char *)malloc((size_t)n);
        if (rgb && alf && (long)off + need <= plen) {
            memset(alf, 255, (size_t)n);
            if (fmt == 0x22)      n2_dxt1(pix + off, w, h, rgb, alf);
            else if (fmt == 0x24) n2_dxt3(pix + off, w, h, rgb, alf);
            else                  n2_dxt5(pix + off, w, h, rgb, alf);
            for (long i = 0; i < n; i++) {
                rgba[i*4] = rgb[i*3]; rgba[i*4+1] = rgb[i*3+1];
                rgba[i*4+2] = rgb[i*3+2]; rgba[i*4+3] = alf[i];
            }
            ok = 1;
        }
        free(rgb); free(alf);
    }
    if (!ok) { free(rgba); return 0; }
    out->w = w; out->h = h; out->rgba = rgba; out->blend = rec[0x4a];
    return 1;
}

static int want_index(const uint32_t *keys, int nkeys, const FengImage *out,
                      uint32_t key) {
    for (int i = 0; i < nkeys; i++) if (keys[i] == key && !out[i].rgba) return i;
    return -1;
}

int feng_tpk_load(const unsigned char *d, long len, const uint32_t *keys,
                  int nkeys, FengImage *out) {
    int filled = 0;
    for (long a = 0; a + 8 <= len; a++) {
        if (!(d[a] == 0 && d[a+1] == 0 && d[a+2] == 0x31 && d[a+3] == 0xb3)) continue;
        long hbeg = a + 8, hend = hbeg + (long)n2_u32(d + a + 4);
        if (hend > len) continue;
        /* pixel payload for plain tables: the next 0x33320002, filler skipped */
        long pix = -1;
        for (long j = hend; j + 8 <= len; j++)
            if (d[j] == 0x02 && d[j+1] == 0 && d[j+2] == 0x32 && d[j+3] == 0x33) {
                pix = j + 8;
                while (pix < len && d[pix] == 0x11) pix++;
                /* the payload starts 0x80-aligned from the chunk header;
                   pixel bytes that are themselves 0x11 must not be eaten
                   (InGameCommon.bun: 3 bytes, ARROW_MARKERA) -- as
                   nfsu2.h n2_tpk_pixel_skip */
                {
                    long run = pix - (j + 8), r = run - (run + 8) % 0x80;
                    if (r >= 0) pix = j + 8 + r;
                }
                break;
            }
        for (long o = hbeg; o + 8 <= hend; ) {
            uint32_t id = n2_u32(d + o), sz = n2_u32(d + o + 4);
            if ((unsigned long)sz > (unsigned long)(hend - o - 8)) break;   /* no signed wrap (audit 11-17) */
            long body = o + 8, bend = body + (long)sz;
            if (id == 0x33310004u && pix >= 0) {              /* plain records */
                for (long r = body; r + 0x7c <= bend; r += 0x7c) {
                    const unsigned char *rec = d + r + 0x0c;
                    int k = want_index(keys, nkeys, out, n2_u32(rec + 0x18));
                    if (k >= 0 && decode_record(rec, d + pix, len - pix, &out[k])) filled++;
                }
            } else if (id == 0x33310003u) {                   /* compressed index */
                for (long e = body; e + 0x18 <= bend; e += 0x18) {
                    int k = want_index(keys, nkeys, out, n2_u32(d + e));
                    if (k < 0) continue;
                    uint32_t off = n2_u32(d + e + 4);
                    const unsigned char *b = NULL;
                    long bases[2] = { a - 8, 0 };   /* relative to the TPK container */
                    for (int bi = 0; bi < 2 && !b; bi++) {
                        long at = bases[bi] + (long)off;
                        if (at >= 0 && at + 16 <= len &&
                            (!memcmp(d + at, "JDLZ", 4) || !memcmp(d + at, "HUFF", 4)))
                            b = d + at;
                    }
                    if (!b) continue;
                    long ulen = (long)n2_u32(b + 8);
                    if (ulen < 0x90 || ulen > 16 * 1024 * 1024) continue;
                    unsigned char *blob = (unsigned char *)malloc((size_t)ulen);
                    if (!blob) continue;
                    long got = !memcmp(b, "JDLZ", 4)
                        ? n2_jdlz(b, (int)(len - (b - d)), blob, (int)ulen)
                        : n2_huff(b + 16, len - (b - d) - 16, blob, ulen);
                    if (got == ulen) {
                        /* blob = pixels from offset 0, then the record (name at
                           len - 0x90); keep the palette's distance from them */
                        unsigned char rec[0x80];       /* decode reads to +0x4a */
                        memcpy(rec, blob + ulen - 0x90, sizeof rec);
                        uint32_t po = n2_u32(rec + 0x24), pp = n2_u32(rec + 0x28);
                        uint32_t zero = 0, rel = pp - po;
                        memcpy(rec + 0x24, &zero, 4); memcpy(rec + 0x28, &rel, 4);
                        if (decode_record(rec, blob, ulen, &out[k])) filled++;
                    }
                    free(blob);
                }
            }
            o = bend;
        }
        a = hend - 1;
    }
    return filled;
}

/* feng_tpk_load from the file itself, for plain-record TPKs: the chunk
   tree walked by headers, each 0xb3310000 header chunk read whole (tens of
   KB), then only the wanted records' pixels (+ palette) -- the 8 MB GLOBALB
   for a screen's few menu images costs a few hundred KB and no big block.
   The pixel base is found as feng_tpk_load finds it (the next 0x33320002
   after the header, 0x11 filler skipped). -1: not a plain chunk file (JDLZ
   bundle, compressed TPK index, unreadable) -- the caller reads the whole
   bundle then (records already filled stay). */
static int tpkf_read(FILE *f, long at, void *dst, long n) {
    return n >= 0 && fseek(f, at, SEEK_SET) == 0 && (long)fread(dst, 1, (size_t)n, f) == n;
}
static int tpkf_header(FILE *f, long flen, long hbeg, long hend, const uint32_t *keys,
                       int nkeys, FengImage *out, int *comp) {
    long hl = hend - hbeg;
    if (hl <= 0 || hl > 4 * 1024 * 1024) return 0;
    unsigned char *h = (unsigned char *)malloc((size_t)hl);
    if (!h || !tpkf_read(f, hbeg, h, hl)) { free(h); return 0; }
    /* the pixel base: the first 0x33320002 within a window after the header */
    long pix = -1;
    {
        enum { WIN = 64 * 1024 };
        long wl = flen - hend < WIN ? flen - hend : WIN;
        unsigned char *w = wl > 8 ? (unsigned char *)malloc((size_t)wl) : NULL;
        if (w && tpkf_read(f, hend, w, wl))
            for (long j = 0; j + 8 <= wl; j++)
                if (w[j] == 0x02 && w[j+1] == 0 && w[j+2] == 0x32 && w[j+3] == 0x33) {
                    long p = j + 8;
                    while (p < wl && w[p] == 0x11) p++;
                    p = j + 8 + n2_tpk_pixel_skip(p - (j + 8));   /* 0x11 pixel bytes stay (as feng_tpk_load) */
                    if (p < wl) pix = hend + p;
                    break;
                }
        free(w);
    }
    int filled = 0;
    for (long o = 0; o + 8 <= hl && pix >= 0; ) {
        uint32_t id = n2_u32(h + o), sz = n2_u32(h + o + 4);
        if ((unsigned long)sz > (unsigned long)(hl - o - 8)) break;   /* no signed wrap (audit 11-17) */
        long body = o + 8, bend = body + (long)sz;
        if (id == 0x33310003u) *comp = 1;            /* compressed: the whole-bundle path */
        if (id == 0x33310004u) {
            for (long r = body; r + 0x7c <= bend; r += 0x7c) {
                const unsigned char *rec = h + r + 0x0c;
                int k = want_index(keys, nkeys, out, n2_u32(rec + 0x18));
                if (k < 0) continue;
                uint32_t off = n2_u32(rec + 0x24), paloff = n2_u32(rec + 0x28), palsz = n2_u32(rec + 0x30);
                int w = rec[0x38] | rec[0x39] << 8, ht = rec[0x3a] | rec[0x3b] << 8;
                unsigned char fmt = rec[0x3e];
                if (w <= 0 || ht <= 0 || w > 2048 || ht > 2048) continue;
                long n = (long)w * ht, bx = (w + 3) / 4, by = (ht + 3) / 4, need, pal = 0;
                if (fmt == 0x08 && palsz >= 1024) { need = n; pal = 1024; }
                else if (fmt == 0x20) need = n * 4;
                else if (fmt == 0x22) need = bx * by * 8;
                else if (fmt == 0x24 || fmt == 0x26) need = bx * by * 16;
                else continue;
                if (pix + (long)off + need > flen || (pal && pix + (long)paloff + pal > flen)) continue;
                unsigned char *buf = (unsigned char *)malloc((size_t)(need + pal));
                if (!buf) continue;
                unsigned char rc[0x7c - 0x0c];
                memcpy(rc, rec, sizeof rc);
                uint32_t z = 0, pr = (uint32_t)need;
                memcpy(rc + 0x24, &z, 4); memcpy(rc + 0x28, &pr, 4);
                if (tpkf_read(f, pix + (long)off, buf, need) &&
                    (!pal || tpkf_read(f, pix + (long)paloff, buf + need, pal)) &&
                    decode_record(rc, buf, need + pal, &out[k])) filled++;
                free(buf);
            }
        }
        o = bend;
    }
    free(h);
    return filled;
}
static int tpkf_walk(FILE *f, long flen, long beg, long end, const uint32_t *keys,
                     int nkeys, FengImage *out, int depth, int *comp) {
    int filled = 0;
    for (long o = beg; o + 8 <= end; ) {
        unsigned char hd[8];
        if (!tpkf_read(f, o, hd, 8)) break;
        uint32_t id = n2_u32(hd), sz = n2_u32(hd + 4);
        if ((unsigned long)sz > (unsigned long)(end - o - 8)) break;   /* no signed wrap (audit 11-17) */
        long body = o + 8, bend = body + (long)sz;
        if (id == 0xb3310000u) filled += tpkf_header(f, flen, body, bend, keys, nkeys, out, comp);
        else if ((id & 0x80000000u) && depth < 4)
            filled += tpkf_walk(f, flen, body, bend, keys, nkeys, out, depth + 1, comp);
        o = bend;
    }
    return filled;
}
int feng_tpk_load_file(const char *path, const uint32_t *keys, int nkeys, FengImage *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char m[4];
    long flen = -1;
    if (fseek(f, 0, SEEK_END) == 0) flen = ftell(f);
    if (flen < 8 || !tpkf_read(f, 0, m, 4) || !memcmp(m, "JDLZ", 4)) { fclose(f); return -1; }
    int comp = 0;
    int filled = tpkf_walk(f, flen, 0, flen, keys, nkeys, out, 0, &comp);
    fclose(f);
    return comp ? -1 : filled;
}

/* ---------------------------------------------------------------- fonts */

int feng_load_font(const unsigned char *lang, long len, const char *resname,
                   FengFont *out) {
    char want[40]; base_lower(resname, want, sizeof want);
    for (long o = 0; o + 8 <= len; ) {
        uint32_t id = n2_u32(lang + o), sz = n2_u32(lang + o + 4);
        if ((unsigned long)sz > (unsigned long)(len - o - 8)) break;   /* no signed wrap (audit 11-17) */
        long body = o + 8, bend = body + (long)sz;
        if (id == 0x00030201u && sz > 0x220) {
            char nm[40]; int n = 0;
            while (n < 39 && lang[body + n]) { nm[n] = (char)tolower(lang[body + n]); n++; }
            nm[n] = 0;
            const unsigned char *f = lang + body + 0x200;
            if (!strcmp(nm, want) && !memcmp(f, "FNTF", 4)) {
                memset(out, 0, sizeof *out);
                memcpy(out->name, nm, sizeof out->name);
                int count = f[0x0a] | f[0x0b] << 8;
                out->line = f[0x12] | f[0x13] << 8;
                uint32_t table = n2_u32(f + 0x14);
                for (int i = 0; i < count; i++) {
                    const unsigned char *g = f + table + (long)i * 16;
                    if (g + 16 > lang + bend) break;
                    unsigned c = g[0] | g[1] << 8;
                    if (c >= 256) continue;              /* ASCII/Latin-1 HUD only */
                    FengGlyph *gl = &out->g[c];
                    gl->w = g[2]; gl->h = g[3];
                    gl->u = (unsigned short)(g[4] | g[5] << 8);
                    gl->v = (unsigned short)(g[6] | g[7] << 8);
                    gl->xo = (signed char)g[9]; gl->yo = g[10];
                    gl->adv = (unsigned short)(g[14] | g[15] << 8);
                    gl->ok = 1;
                }
                char up[40];
                for (n = 0; nm[n]; n++) up[n] = (char)toupper((unsigned char)nm[n]);
                up[n] = 0;
                out->atlas_key = feng_hash(up);
                return 1;
            }
        }
        o = bend;
    }
    return 0;
}

float feng_text_width(const FengFont *f, const char *s) {
    float w = 0.0f;
    for (; *s; s++) {
        const FengGlyph *g = &f->g[(unsigned char)*s];
        if (g->ok) w += g->adv;
    }
    return w;
}
