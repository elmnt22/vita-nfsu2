/* vu_shim.inc -- the GL shims of SDL_opengles2.h (uniform cache, program
   variants, depth-of-field mask), shared by sdl_vita.c and the Windows
   simulator (sim/sim_vita.c). Included, not compiled on its own. */
/* ---- GL shims (see SDL_opengles2.h) --------------------------------------- */
#define VU_SLOTS 4096        /* power of two; open addressing */
/* known: v holds what the program itself has for loc (set by a write that
   reached it, cleared when the program is linked again) */
typedef struct { GLuint prog; GLint loc; int n; int known; float v[16]; } VuSlot;
static VuSlot vu_cache[VU_SLOTS];
static GLuint vu_prog;

static VuSlot *vu_slot(GLuint prog, GLint loc, int create) {
    uint32_t h = (prog * 2654435761u) ^ ((uint32_t)loc * 40503u);
    for (int i = 0; i < VU_SLOTS; i++) {
        VuSlot *s = &vu_cache[(h + (uint32_t)i) & (VU_SLOTS - 1)];
        if (s->prog == prog && s->loc == loc) return s;
        if (s->prog == 0) {
            if (!create) return NULL;
            s->prog = prog; s->loc = loc; s->n = 1; s->known = 0; memset(s->v, 0, sizeof s->v);
            return s;
        }
    }
    return NULL;
}

int vita_add_variant(GLuint prog, int key, const char *const *names, int n);
extern int vita_gfx_mode;
/* Program variants. The main program is the logical program the engine
   talks to: its uniform locations are what callers hold, and every write is
   cached under it. Variants are smaller programs compiled from the same
   shader with unused paths removed (render.c). Each one maps the main
   program's uniform names to its own locations; writes go to whichever
   program is bound, and a per-uniform version number lets a newly bound
   variant receive only the values it missed.
   Variant 0 is the main program itself, variant 1 the world lite program
   (forced by vita_use_lite), the rest are picked per draw from the uniform
   values that select the shader's paths (vu_select). */
#define VU_NAMES 64
#define VU_VARIANTS 28
#define VU_KEYS 25
typedef struct { GLuint prog; GLint to[VU_NAMES]; unsigned synced[VU_NAMES]; } VuVar;
static GLuint vu_main;
static GLint vu_from[VU_NAMES];
static unsigned vu_ver[VU_NAMES];
static float vu_first[VU_NAMES];          /* first component of the last write */
static int vu_nn;
static VuVar vu_var[VU_VARIANTS];
static int vu_nvar, vu_bound, vu_forced = -1;
static int vu_reflect;   /* vita_set_reflect */
static int vu_sky;       /* vita_set_sky */
static int vu_nofast = -1;   /* ux0:data/NFSU2/nofast.txt: no keys 23/24, for A/B */
static signed char vu_key_var[VU_KEYS];   /* key -> variant (0 = main program) */
enum { VS_UVCHECK, VS_VISTA, VS_EMTEX, VS_UNLIT, VS_SPEC, VS_CLEARCOAT, VS_ENV, VS_FRESNEL,
       VS_ALPHATEST, VS_CARFX, VS_VCOLOR, VS_USETEX, VS_N };
static const char *const vu_sel_name[VS_N] = { "uUVCheck", "uVista", "uEmissiveTex", "uUnlit",
                                               "uSpec", "uClearcoat", "uEnv", "uFresnel",
                                               "uAlphaTest", "uCarFx", "uVColor", "uUseTex" };
static int vu_sel[VS_N];
static int vu_dofw = -1;                   /* uDofW's name index (depth of field) */
static void vu_dof_sync(void);
static unsigned vu_switches;               /* cumulative variant binds */
#define VU_HASH 128
static struct { GLint loc; int idx; } vu_hash[VU_HASH];

static int vu_index(GLint loc) {
    for (unsigned i = 0, h = (unsigned)loc * 2654435761u >> 25; i < VU_HASH; i++) {
        unsigned k = (h + i) & (VU_HASH - 1);
        if (vu_hash[k].idx < 0) return -1;
        if (vu_hash[k].loc == loc) return vu_hash[k].idx;
    }
    return -1;
}

void vu_UseProgram(GLuint prog);

static void vu_sync(int v) {
    VuVar *x = &vu_var[v];
    for (int i = 0; i < vu_nn; i++) {
        if (x->synced[i] == vu_ver[i]) continue;
        x->synced[i] = vu_ver[i];
        GLint l = x->to[i];
        VuSlot *s = vu_slot(vu_main, vu_from[i], 0);
        if (l == -1 || !s) continue;
        switch (s->n) {
        case 1:  glUniform1f(l, s->v[0]); break;
        case 3:  glUniform3fv(l, 1, s->v); break;
        case 4:  glUniform4fv(l, 1, s->v); break;
        case 16: glUniformMatrix4fv(l, 1, GL_FALSE, s->v); break;
        }
    }
}

static void vu_bind(int v) {
    if (v == vu_bound) return;
    glUseProgram(vu_var[v].prog);   /* raw vitaGL call: this file is not shimmed */
    vu_bound = v;
    vu_switches++;
    vu_sync(v);
}

void vu_UseProgram(GLuint prog) {
    vu_prog = prog;
    if (prog == vu_main && vu_main) { vu_bound = 0; vu_forced = -1; vu_sync(0); }
}

/* Registers the main program with the full list of its uniform names and the
   world lite program as variant 1 (names it lacks map to -1, i.e. dropped). */
void vita_alias_program(GLuint main_prog, GLuint lite_prog, const char *const *names, int n) {
    vu_main = main_prog; vu_nn = 0; vu_nvar = 0; vu_bound = 0; vu_forced = -1;
    for (int k = 0; k < VU_HASH; k++) vu_hash[k].idx = -1;
    for (int i = 0; i < n && vu_nn < VU_NAMES; i++) {
        GLint f = glGetUniformLocation(main_prog, names[i]);
        if (f == -1) continue;
        vu_from[vu_nn] = f; vu_ver[vu_nn] = 1;   /* sync every value on first bind */
        for (unsigned j = 0, h = (unsigned)f * 2654435761u >> 25; j < VU_HASH; j++) {
            unsigned k = (h + j) & (VU_HASH - 1);
            if (vu_hash[k].idx < 0) { vu_hash[k].loc = f; vu_hash[k].idx = vu_nn; break; }
        }
        vu_nn++;
    }
    for (int s = 0; s < VS_N; s++) {
        vu_sel[s] = vu_index(glGetUniformLocation(main_prog, vu_sel_name[s]));
    }
    vu_dofw = vu_index(glGetUniformLocation(main_prog, "uDofW"));
    /* values written before registration live in the cache only */
    for (int i = 0; i < vu_nn; i++) {
        VuSlot *s = vu_slot(main_prog, vu_from[i], 0);
        vu_first[i] = s ? s->v[0] : 0.0f;
    }
    memset(vu_key_var, 0, sizeof vu_key_var);
    vu_var[0].prog = main_prog;
    for (int i = 0; i < vu_nn; i++) { vu_var[0].to[i] = vu_from[i]; vu_var[0].synced[i] = 1; }
    vu_nvar = 1;
    vita_add_variant(lite_prog, -1, names, n);
}

/* key: 0-7 lit with bits 1 spec, 2 env, 4 fresnel; 8 textured unlit; 9 flat unlit;
   10-17 the lit keys with alpha-test discard; 18 world lite without discard;
   -1 = only reachable through vita_use_lite. Returns the variant index. */
int vita_add_variant(GLuint prog, int key, const char *const *names, int n) {
    (void)names; (void)n;
    if (!vu_main || vu_nvar >= VU_VARIANTS) return -1;
    VuVar *x = &vu_var[vu_nvar];
    x->prog = prog;
    for (int i = 0; i < vu_nn; i++) {
        x->synced[i] = 0;
        x->to[i] = -1;
    }
    /* resolve by name: look each main location's name up through the caller's list */
    for (int j = 0; j < n; j++) {
        int i = vu_index(glGetUniformLocation(vu_main, names[j]));
        if (i >= 0) x->to[i] = glGetUniformLocation(prog, names[j]);
    }
    if (key >= 0 && key < VU_KEYS) vu_key_var[key] = (signed char)vu_nvar;
    return vu_nvar++;
}

static inline float vu_selv(int s) { return vu_sel[s] >= 0 ? vu_first[vu_sel[s]] : 0.0f; }

/* Called before every draw while the main program is current. */
void vu_select(void) {
    if (!vu_main || vu_prog != vu_main) return;
    /* A program that can discard loses the GPU's hidden-surface removal for
       every draw it runs, so opaque draws use variants without it. */
    if (vu_forced >= 0) {
        int v = vu_forced;
        if (vu_nofast < 0) {
            SceUID fd = sceIoOpen("ux0:data/NFSU2/nofast.txt", SCE_O_RDONLY, 0);
            vu_nofast = fd >= 0;
            if (fd >= 0) sceIoClose(fd);
            printf("world shaders: %s%c", vu_nofast ? "sky/world fast paths off (nofast.txt)"
                                                     : "opaque sky, world.fx fast path", 10);
        }
        if (v == 1 && vu_sky && !vu_nofast && vu_selv(VS_UNLIT) <= 0.5f && vu_selv(VS_EMTEX) > 0.5f
            && vu_key_var[23] > 0)
            v = vu_key_var[23];   /* opaque sky dome: no discard */
        else if (v == 1 && vu_selv(VS_UNLIT) <= 0.5f && vu_selv(VS_EMTEX) <= 0.5f) {
            if (vu_selv(VS_ALPHATEST) <= 0.5f) {
                float vc = vu_selv(VS_VCOLOR);
                if (vu_reflect && vu_key_var[20] > 0) v = vu_key_var[20];   /* wet road */
                else if (!vu_nofast && vu_key_var[24] > 0 && vu_selv(VS_USETEX) > 0.5f
                         && vc > 0.001f && vc <= 1.5f)
                    v = vu_key_var[24];   /* world.fx: sat(2*tex*vcol) */
                else if (vu_key_var[18] > 0) v = vu_key_var[18];
            }
            else if (vu_key_var[19] > 0 && vita_gfx_mode != 2) v = vu_key_var[19];
        }
        vu_bind(v); vu_dof_sync(); return;
    }
    int key;
    if (vu_selv(VS_UVCHECK) > 0.5f || vu_selv(VS_VISTA) > 0.5f) key = -1;
    else if (vu_selv(VS_EMTEX) > 0.5f) key = 8;
    else if (vu_selv(VS_UNLIT) > 0.5f) key = 9;
    else if (vu_selv(VS_CARFX) > 0.5f && vu_key_var[21] > 0)   /* car.fx */
        key = vu_selv(VS_ALPHATEST) > 0.5f && vu_key_var[22] > 0 ? 22 : 21;
    else key = ((vu_selv(VS_SPEC) > 0.001f || vu_selv(VS_CLEARCOAT) > 0.001f ? 1 : 0)
                | (vu_selv(VS_ENV) > 0.001f ? 2 : 0) | (vu_selv(VS_FRESNEL) > 0.5f ? 4 : 0))
             + (vu_selv(VS_ALPHATEST) > 0.5f ? 10 : 0);   /* 10-17: added, 10|2 would lose env */
    vu_bind(key >= 0 ? vu_key_var[key] : 0);
    vu_dof_sync();
}

/* the next opaque world draws are reflective road (variant 20) */
void vita_set_reflect(int on) { vu_reflect = on; }
/* the next draws are the opaque sky dome (variant 23) */
void vita_set_sky(int on) { vu_sky = on; }
void vita_use_lite(int on) {
    if (!vu_main || vu_nvar < 2 || vu_prog != vu_main) return;
    vu_forced = on ? 1 : -1;
    if (on) vu_bind(1);
}

unsigned vita_variant_switch_total(void) { return vu_switches; }
unsigned vita_variant_switches(void) {
    static unsigned previous;
    unsigned n = vu_switches - previous;
    previous = vu_switches;
    return n;
}

/* Only element 0 of an array upload is recorded, under the base location:
   that is what glGetUniformfv returns for it in GL. Storing element e at
   loc+e assumed vitaGL numbers array elements consecutively; when loc+1 is
   really the next uniform, a later read of that (scalar) uniform copied a
   whole matrix into the caller's one-float variable and smashed the stack. */
/* A write of the value the program already holds is dropped: vitaGL marks
   the whole uniform buffer dirty on any write and copies it again at the
   next draw, so repeated per-mesh state (colour, spec, unlit 0 ...) cost a
   reserve + copy per draw. Only single-element writes are compared (arrays
   keep element 0 only); variants are written behind the cache by vu_sync,
   so a variant bound directly is never skipped. */
static int vu_skip_off;   /* simulator A/B: OPENUG2_SIM_NOUSKIP */
static int vu_is_variant(GLuint prog) {
    for (int k = 1; k < vu_nvar; k++) if (vu_var[k].prog == prog) return 1;
    return 0;
}
GLint vu_Uniform(GLint loc, int n, int count, const GLfloat *v) {
    if (loc == -1 || !vu_prog || !v || count < 1 || n < 1 || n > 16) return loc;
    VuSlot *s = vu_slot(vu_prog, loc, 1);
    int same = !vu_skip_off && s && s->known && count == 1 && s->n == n && !memcmp(s->v, v, (size_t)n * sizeof *v);
    if (s) { s->n = n; memcpy(s->v, v, (size_t)n * sizeof *v); }
    if (vu_prog != vu_main || !vu_main) {
        if (same && !vu_is_variant(vu_prog)) return -1;
        if (s) s->known = 1;
        return loc;
    }
    int i = vu_index(loc);
    if (i < 0) { if (s) s->known = 0; return vu_bound == 0 ? loc : -1; }
    /* the bound variant has every value up to vu_ver (vu_bind syncs) */
    if (same && vu_var[vu_bound].synced[i] == vu_ver[i]) return -1;
    if (s) s->known = 1;
    vu_first[i] = v[0];
    vu_var[vu_bound].synced[i] = ++vu_ver[i];
    return vu_var[vu_bound].to[i];
}

/* Linking resets a program's uniforms (and a deleted program's name can come
   back for a new one): forget what the cache knows about it. */
void vu_LinkProgram(GLuint prog) {
    glLinkProgram(prog);   /* raw vitaGL call: this file is not shimmed */
    for (int k = 0; k < VU_SLOTS; k++) if (vu_cache[k].prog == prog) vu_cache[k].known = 0;
}

/* Copies exactly the uniform's own component count (1, 3, 4 or 16), which is
   what the caller's buffer is sized for. Never-set uniforms read as one 0. */
void vu_GetUniformfv(GLuint prog, GLint loc, GLfloat *out) {
    VuSlot *s = loc >= 0 ? vu_slot(prog, loc, 0) : NULL;
    if (s) memcpy(out, s->v, (size_t)s->n * sizeof *out);
    else out[0] = 0.0f;
}

/* Depth of field (render.c render_dof_scene): while on, opaque draws write the
   depth to the frame's alpha (uDofW 1) and blended draws leave the alpha
   alone (the original's "nodepth" effects), so the colour mask's alpha bit is
   off whenever blending is on. The caller's own mask is kept and reported. */
static int vu_dof, vu_blend_on;
static GLboolean vu_cmask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
static void vu_apply_mask(void) {
    glColorMask(vu_cmask[0], vu_cmask[1], vu_cmask[2], vu_cmask[3] && !(vu_dof && vu_blend_on));
}
void vu_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    vu_cmask[0] = r; vu_cmask[1] = g; vu_cmask[2] = b; vu_cmask[3] = a;
    vu_apply_mask();
}
void vu_GetBooleanv(GLenum pname, GLboolean *out) {
    if (pname == GL_COLOR_WRITEMASK) { memcpy(out, vu_cmask, sizeof vu_cmask); return; }
    glGetBooleanv(pname, out);
}
void vita_set_dof(int on) {
    on = on != 0;
    if (on == vu_dof) return;
    vu_dof = on;
    vu_blend_on = glIsEnabled(GL_BLEND) != GL_FALSE;
    vu_apply_mask();
}
/* uDofW for the bound variant: 1 for opaque draws while the depth of field
   is on. Written like a caller's glUniform1f so the other variants sync. */
static void vu_dof_sync(void) {
    int i = vu_dofw;
    float v = vu_dof && !vu_blend_on ? 1.0f : 0.0f;
    if (i < 0 || vu_first[i] == v) return;
    VuSlot *s = vu_slot(vu_main, vu_from[i], 1);
    if (s) { s->n = 1; s->v[0] = v; }
    vu_first[i] = v;
    vu_var[vu_bound].synced[i] = ++vu_ver[i];
    if (vu_var[vu_bound].to[i] != -1) glUniform1f(vu_var[vu_bound].to[i], v);
}

void vu_Enable(GLenum cap) {
    if (cap == GL_DITHER) return;
    glEnable(cap);
    if (cap == GL_BLEND) { vu_blend_on = 1; if (vu_dof) vu_apply_mask(); }
}
void vu_Disable(GLenum cap) {
    if (cap == GL_DITHER) return;
    glDisable(cap);
    if (cap == GL_BLEND) { vu_blend_on = 0; if (vu_dof) vu_apply_mask(); }
}
GLboolean vu_IsEnabled(GLenum cap) { return cap == GL_DITHER ? GL_FALSE : glIsEnabled(cap); }
