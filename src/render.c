/* render.c — OpenUG2 Renderer module implementation. Everything here was
 * extracted verbatim from the original monolithic main.c; the parsing logic
 * (nfsu2.h) is untouched ground truth. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <assert.h>
#include <zlib.h>   /* screenshot PNG (dev only) */

#include "render.h"
#include "carfx.h"
#ifdef __vita__
void hitch_mark(const char *label);   /* world_resident.c frame-span profiler */
#endif

#if defined(__APPLE__) && !defined(N2_GLES)
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define glGenRenderbuffers glGenRenderbuffersEXT
#define glBindRenderbuffer glBindRenderbufferEXT
#define glRenderbufferStorage glRenderbufferStorageEXT
#define glFramebufferRenderbuffer glFramebufferRenderbufferEXT
#define glDeleteRenderbuffers glDeleteRenderbuffersEXT
#endif

#ifdef N2_GLES
#  define GLSL_HEADER "precision mediump float;\n"
/* Vertex shaders whose positions are absolute world coordinates (|x| up to
   ~2000 m): at mediump the clip z of geometry near the camera is off by
   metres, and a road or kerb under the chase camera wrote a depth in front
   of the player's car and hid it (career free roam at the airport lot, on
   GLES drivers that honour mediump in vertex shaders). The car's own shader
   works in model space and keeps GLSL_HEADER. */
#  define GLSL_VS_HEADER "precision highp float;\n"
#else
#  define GLSL_HEADER "#version 120\n#define lowp\n#define mediump\n#define highp\n"
#  define GLSL_VS_HEADER GLSL_HEADER
#endif
/* Depth of field (render_set_dof): with it on, the original draws every
   opaque world / car / character mesh a second time with the effect's depth
   pass (world.fx: alpha = saturate(viewZ * FocalRange), FocalRange 1/1024).
   Here the colour pass writes that alpha itself while uDofW is 1 (the Vita
   shim sets it per draw: opaque draws of the main 3D pass only). */
#define DOF_ALPHA(a) "mix(" a ",clamp(vDepth*0.0009765625,0.0,1.0),uDofW)"

/* World glows use world coordinates, never the preceding car/smoke matrix.
   Additive fog fades to black so sprite rectangles cannot add fog colour. */
int render_world_glows(const RProg *r,const N2Batch *batches,int count,
                        const float mvp[16]) {
    float fog[3];glGetUniformfv(r->prog,r->uFogColor,fog);
    render_model(r,NULL);
    glUniformMatrix4fv(r->uMVP,1,GL_FALSE,mvp);
    glUniform1f(r->uAlpha,1);glUniform1f(r->uSoft,0);
    glUniform3f(r->uFogColor,0,0,0);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    int draws=0;
    for(int i=0;i<count;i++) {
        const N2Batch *b=&batches[i];
        if(b->unresolved)continue; /* no texture-backed flare mask available */
        glUniform1f(r->uUnlit,b->tex?0:1);
        glUniform1f(r->uUseTex,b->tex?1:0);
        glUniform1f(r->uEmissiveTex,b->tex?1:0);
        glUniform3f(r->uColor,1,b->tex?1:.85f,b->tex?1:.5f);
        glBindTexture(GL_TEXTURE_2D,b->tex);
        draw_batch(b);draws++;
    }
    glUniform3fv(r->uFogColor,1,fog);
    glUniform1f(r->uEmissiveTex,0);glUniform1f(r->uUnlit,0);
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    return draws;
}

int render_district_lights(const RProg *r, GpuMesh *quad, GLuint texture,
                           const N2LightSrc *lights, int nlights,
                           const float cam[3], const float look[3],
                           const float MVP[16], float viewdist,
                           float halo, float gain) {
    RProg rp = *r;
    int draws = 0;
    const GLint scalar_loc[] = {rp.uUnlit, rp.uEmissiveTex, rp.uUseTex,
                                rp.uSoft, rp.uAlpha};
    float scalars[5], color[3], fog_color[3], saved_mvp[16];
    for (int i=0; i<5; i++) glGetUniformfv(rp.prog,scalar_loc[i],scalars+i);
    glGetUniformfv(rp.prog,rp.uColor,color);
    glGetUniformfv(rp.prog,rp.uFogColor,fog_color);
    glGetUniformfv(rp.prog,rp.uMVP,saved_mvp);
    GLint texture_before, blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture_before);
    glGetIntegerv(GL_BLEND_SRC_RGB,&blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB,&blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&blend_src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA,&blend_dst_a);
    GLboolean blend_before=glIsEnabled(GL_BLEND), depth_before=glIsEnabled(GL_DEPTH_TEST);
    GLboolean depth_mask; glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
    float ll = sqrtf(look[0]*look[0] + look[1]*look[1] + look[2]*look[2]);
    if (ll < 1e-4f) ll = 1.0f;
    float ld[3] = {look[0]/ll, look[1]/ll, look[2]/ll};
    float rt[3] = {ld[1], -ld[0], 0};
    float rl = sqrtf(rt[0]*rt[0] + rt[1]*rt[1]);
    if (rl < 1e-4f) rl = 1.0f;
    rt[0] /= rl; rt[1] /= rl;
    float up[3] = {rt[1]*ld[2], -rt[0]*ld[2],
                   rt[0]*ld[1] - rt[1]*ld[0]};
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glUniform1f(rp.uUnlit, 0.0f); glUniform1f(rp.uEmissiveTex, 1.0f);
    glUniform1f(rp.uUseTex, 1.0f); glUniform1f(rp.uSoft, 0.0f);
    /* Additive emission fades toward zero, not fog RGB: black sprite edges
       must add nothing even at long distance. The sky path is unaffected. */
    glUniform3f(rp.uFogColor,0.0f,0.0f,0.0f);
    glBindTexture(GL_TEXTURE_2D, texture);
    float maxd2 = viewdist * viewdist;
    for (int i = 0; i < nlights; i++) {
        const N2LightSrc *light = &lights[i];
        float dx = light->pos[0]-cam[0], dy = light->pos[1]-cam[1],
              dz = light->pos[2]-cam[2];
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > maxd2) continue;
        float d = sqrtf(d2); if (d < 0.5f) d = 0.5f;
        /* outside a 60-degree cone round the view axis (wider than the
           diagonal half-FOV) plus the largest sprite: off screen */
        if (dx*ld[0] + dy*ld[1] + dz*ld[2] < 0.5f*d - 8.0f*halo) continue;
        /* r_in (10 m on the shipped records) is the light's influence radius,
           NOT a sprite size. Drawing the quad that large buries it in its own
           lamp post and intersects the ground and nearby walls, and the depth
           test then slices it into hard-edged polygons -- the "solid shapes"
           dotted around the night city. A flare is a camera-facing halo: keep
           it at a near-constant angular size, bounded by the authored radius. */
        float s = d * 0.070f * halo;
        float smax = (light->r_in > 1.0f ? light->r_in : 10.0f) * 0.35f * halo;
        if (s > smax) s = smax;
        if (s < 0.45f) s = 0.45f;
        /* Lift it off its own fixture so the lamp geometry cannot clip it,
           while the depth test still hides lights behind buildings. */
        float ox = -ld[0]*s*0.9f, oy = -ld[1]*s*0.9f, oz = -ld[2]*s*0.9f;
        /* Fade over the last quarter of the view range instead of popping. */
        float fade = (viewdist - d) / (viewdist * 0.25f);
        if (fade > 1.0f) fade = 1.0f; else if (fade < 0.0f) fade = 0.0f;
        float M[16] = {
            rt[0]*s,rt[1]*s,rt[2]*s,0,
            up[0]*s,up[1]*s,up[2]*s,0,
            0,0,1,0,
            light->pos[0]+ox-(rt[0]+up[0])*s*0.5f,
            light->pos[1]+oy-(rt[1]+up[1])*s*0.5f,
            light->pos[2]+oz-(rt[2]+up[2])*s*0.5f,1
        };
        float LMVP[16]; mat_mul(MVP, M, LMVP);
        glUniformMatrix4fv(rp.uMVP, 1, GL_FALSE, LMVP);
        glUniform3f(rp.uColor,
            (float)( light->rgba        & 0xffu) / 255.0f,
            (float)((light->rgba >> 8)  & 0xffu) / 255.0f,
            (float)((light->rgba >> 16) & 0xffu) / 255.0f);
        float a = (float)((light->rgba >> 24) & 0xffu) / 255.0f * gain * fade;
        if (a > 1.0f) a = 1.0f;
        if (a <= 0.002f) continue;
        glUniform1f(rp.uAlpha, a);
        draw_gpumesh(quad); draws++;
    }
    for (int i=0; i<5; i++) glUniform1f(scalar_loc[i],scalars[i]);
    glUniform3fv(rp.uColor,1,color);
    glUniform3fv(rp.uFogColor,1,fog_color);
    glUniformMatrix4fv(rp.uMVP,1,GL_FALSE,saved_mvp);
    glBindTexture(GL_TEXTURE_2D,(GLuint)texture_before);
    glBlendFuncSeparate((GLenum)blend_src_rgb,(GLenum)blend_dst_rgb,
                        (GLenum)blend_src_a,(GLenum)blend_dst_a);
    if (blend_before) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth_before) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depth_mask);
    return draws;
}

static const char *VS =
    GLSL_VS_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec3 aNor; attribute vec4 aColor;\n"
    "uniform mat4 uMVP; uniform mat4 uModel; varying vec2 vUV; varying vec3 vN; varying float vDepth;\n"
    "varying vec3 vPos; varying vec4 vColor;\n"
    /* clip.w == view-space depth under a perspective projection, for world
       (P*V) and car (P*V*M) alike — no view matrix or extra uniforms needed.
       NDC-drawn HUD quads have w==1, so fog leaves them alone. */
    "void main(){ vUV=aUV; vN=(uModel*vec4(aNor,0.0)).xyz;\n"
    "  vPos=(uModel*vec4(aPos,1.0)).xyz; vColor=aColor;\n"
    "  gl_Position=uMVP*vec4(aPos,1.0); vDepth=gl_Position.w; }\n";

/* car.fx (SPEED2.EXE, decoded 10-09; carfx.c builds the inputs). Lighting is
   per vertex like the original vs_1_1: D = SH(N), S = SH(R) with R the
   reflected view vector, both from the 10-term quadratic form; oD0/oD1 are
   colour interpolators, so they saturate. World space here (the game works
   in view space; same result). */
#define CARFX_UNIFORMS \
    "uniform vec4 uSH0, uSH1, uSH2, uSH3, uSH4, uSH5, uSH6, uSH7, uSH8, uSH9;\n" \
    "uniform vec4 uCDiffMin, uCDiffRange; uniform vec3 uCSpecMin, uCSpecRange, uCPow;\n"
static const char *VS_CAR =
    GLSL_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec3 aNor; attribute vec4 aColor;\n"
    "uniform mat4 uMVP; uniform mat4 uModel; uniform vec3 uCamPos; uniform float uFlipN;\n"
    CARFX_UNIFORMS
    "varying vec2 vUV; varying float vDepth; varying vec4 vColor;\n"
    "varying vec4 vD; varying vec4 vS; varying vec4 vR;\n"
    "vec4 sh(vec3 n){ return uSH0+uSH1*(n.x*n.x)+uSH2*(n.y*n.y)+uSH3*(n.z*n.z)+uSH4*(n.x*n.y)\n"
    "  +uSH5*(n.x*n.z)+uSH6*(n.y*n.z)+uSH7*n.x+uSH8*n.y+uSH9*n.z; }\n"
    "void main(){\n"
    "  vec3 P=(uModel*vec4(aPos,1.0)).xyz; vec3 N=normalize((uModel*vec4(aNor,0.0)).xyz);\n"
    "  if(uFlipN>0.5) N=-N;\n"
    "  vec3 V=normalize(uCamPos-P); float ndv=max(dot(N,V),0.01);\n"
    "  vec4 D=sh(N); vec3 R=2.0*ndv*N-V; vec4 S=sh(R);\n"
    "  vec4 dl=uCDiffMin+uCDiffRange*ndv;\n"
    "  vD=clamp(vec4(D.rgb*dl.rgb,dl.a)*0.5,0.0,1.0);\n"
    "  float sp=pow(ndv,uCPow.x);\n"
    "  vS.rgb=clamp(S.rgb*pow(clamp(S.w,0.0,1.0),uCPow.x)*(uCSpecMin+uCSpecRange*sp)*0.5,0.0,1.0);\n"
    "  vS.a=pow(ndv,uCPow.y);\n"
    "  vR=vec4(R,ndv); vUV=aUV; vColor=aColor;\n"
    "  gl_Position=uMVP*vec4(aPos,1.0); vDepth=gl_Position.w; }\n";
/* ps_1_1: r0 = 2*env*(EnvMin + EnvRange*oD1.w) + oD1.rgb; r1 = 2*tex*oD0;
   out = r1 + 2*r0*r1.w, alpha r1.w. The cube is a 3x2 atlas of 64 px faces
   (render_env_*): +X -X +Y / -Y +Z -Z. */
static const char *FS_CAR_HEAD =
    GLSL_HEADER
    "varying vec2 vUV; varying float vDepth; varying vec4 vColor;\n"
    "varying vec4 vD; varying vec4 vS; varying vec4 vR;\n"
    "uniform sampler2D uTex; uniform sampler2D uEnvTex;\n"
    "uniform float uUseTex, uDecal, uAlpha, uVColor, uFogDensity, uFogK, uFogFloor, uTextureAlpha, uRimTint;\n"
    "uniform float uDofW;\n"
    "uniform vec3 uColor, uFogColor, uEmissive, uCEnvMin, uCEnvRange;\n"
    "vec2 cubeuv(vec3 r){\n"
    "  vec3 a=abs(r); vec2 uv; float fcol; float frow;\n"
    "  if(a.x>=a.y && a.x>=a.z){ uv=vec2(r.x>0.0?-r.y:r.y, r.z)/a.x; fcol=r.x>0.0?0.0:1.0; frow=0.0; }\n"
    "  else if(a.y>=a.z){ uv=vec2(r.y>0.0?r.x:-r.x, r.z)/a.y; fcol=r.y>0.0?2.0:0.0; frow=r.y>0.0?0.0:1.0; }\n"
    "  else { uv=vec2(r.z>0.0?-r.x:r.x, r.y)/a.z; fcol=r.z>0.0?1.0:2.0; frow=1.0; }\n"
    "  uv=uv*(0.5*62.0/64.0)+0.5;\n"
    "  return vec2((fcol+uv.x)*(1.0/3.0),(frow+uv.y)*0.5); }\n"
    "void main(){\n"
    "  float fd=vDepth*uFogDensity; float fog=clamp(1.0-fd+uFogK,uFogFloor,1.0);\n"
    "  vec4 t=texture2D(uTex,vUV);\n";
static const char *FS_CAR_TAIL =
    "  vec4 tex=uUseTex>0.5?(uDecal>0.5?vec4(mix(uColor,t.rgb,t.a),1.0):t):vec4(uColor,1.0);\n"
    "  if(uVColor>1.5) tex.rgb*=vColor.rgb;\n"
    "  if(uRimTint>0.001){ float l=dot(t.rgb,vec3(0.299,0.587,0.114)); tex.rgb=mix(tex.rgb,uColor*(0.25+1.5*l),uRimTint); }\n"
    "  vec3 env=texture2D(uEnvTex,cubeuv(vR.xyz)).rgb;\n"
    "  vec3 r0=clamp(2.0*env*(uCEnvMin+uCEnvRange*vS.a)+vS.rgb,0.0,1.0);\n"
    "  vec4 r1=clamp(2.0*tex*vD,0.0,1.0);\n"
    "  vec3 c=clamp(r1.rgb+2.0*r0*r1.a,0.0,1.0);\n"
    /* a textured lamp glows where its art is bright (the bulbs), not over the whole housing */
    "  if(dot(uEmissive,uEmissive)>0.0) c+=uEmissive*(0.55+0.45*vR.w)*(uUseTex>0.5?clamp(2.0*dot(t.rgb,vec3(0.299,0.587,0.114)),0.0,1.0):1.0);\n"
    "  float a=uTextureAlpha>0.5?r1.a*uAlpha:uAlpha;\n"
    "  gl_FragColor=vec4(mix(uFogColor,c,fog)," DOF_ALPHA("a") ");\n}\n";

static const char *FS =
    GLSL_HEADER
    "varying vec2 vUV; varying vec3 vN; varying float vDepth; varying vec3 vPos;\n"
    "varying vec4 vColor;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uVColor;\n"   /* 0 off, 1 world MODULATE2X, 2 car diffuse */
    "uniform float uUseTex; uniform vec3 uColor; uniform float uUnlit; uniform float uAlpha; uniform float uSoft; uniform float uSpec; uniform float uDecal;\n"
    "uniform float uAmbient; uniform float uDiffuse; uniform vec3 uLight;\n"
    "uniform vec3 uFogColor; uniform float uFogDensity, uFogK, uFogFloor;\n"
    "uniform vec3 uCamPos; uniform float uEnv; uniform float uUVCheck;\n"
    "uniform float uGloss; uniform float uFlipN;\n"
    "uniform float uRimTint;\n"   /* >0: recolor the rim diffuse toward uColor */
    "uniform vec3 uEmissive;\n"   /* lamp emission added on top of the lit result */
    "uniform float uVista;\n"     /* >0.5: authored backdrop pass, alpha-blended */
    "uniform float uAlphaTest;\n" /* >0.5: discard below 0.5 texture alpha */
    "uniform float uTextureAlpha;\n" /* >0.5: output alpha = texture2D(...).a *
        uAlpha instead of plain uAlpha (M135-R: N2_DRAW_BLEND/ADD world
        batches only -- everything else, including CUTOUT, cars, and every
        HUD/debug uUnlit pass, leaves this at its default 0.0 and keeps its
        existing uAlpha-only alpha output unchanged) */
    "uniform float uEmissiveTex;\n" /* authored texture-backed unlit pass */
    "uniform float uFresnel;\n"   /* >0.5: glass -- fresnel drives alpha + reflection */
    "uniform float uClearcoat;\n" /* >0: second tight specular lobe over the base coat */
    "uniform float uDofW;\n"      /* 1: alpha = depth of field depth (DOF_ALPHA) */
    /* car.fx inputs: declared and touched here only so the main program
       owns their locations (the variant system maps names through it) */
    "uniform float uCarFx;\n"
    CARFX_UNIFORMS
    "uniform vec3 uCEnvMin, uCEnvRange;\n"
    "uniform vec4 uHeadPos[2];\n"
    "uniform vec3 uHeadForward, uHeadRight, uHeadUp;\n"
    "uniform vec4 uHeadShape;\n" /* horizontal/vertical spread, downward slope, range */
    "uniform float uHeadGain, uHeadLow;\n"
    "uniform vec3 uHeadColor;\n"   /* the bulb: HEADLIGHTCOLOR */
    "uniform float uHeadShadow;\n"
    "uniform sampler2D uHeadShadowMap0, uHeadShadowMap1;\n"
    "uniform mat4 uHeadShadowMVP[2];\n"
    "float headVisibility(int h,vec3 position,vec3 normal,float along){\n"
    "  if(uHeadShadow<0.5)return 1.0;\n"
    "  float offset=0.005+along*max(uHeadShape.x,uHeadShape.y)*2.0/512.0;\n"
    /* Constant indices only: Cg fragment programs (PS Vita) cannot index a
       uniform array with a variable, so the lamp is selected with ?: instead. */
    "  mat4 shadowMVP=h==0?uHeadShadowMVP[0]:uHeadShadowMVP[1];\n"
    "  vec4 clip=shadowMVP*vec4(position+normal*offset,1.0);\n"
    "  vec2 uv=clip.xy/clip.w*0.5+0.5;\n"
    "  if(clip.w<=0.0 || uv.x<0.0 || uv.x>1.0 || uv.y<0.0 || uv.y>1.0)return 1.0;\n"
    "  float depth=(clip.w-0.025-0.001*along)/uHeadShape.w;\n"
    "  float visibility=0.0;\n"
    /* Distinct names: vitaGL's GLSL->Cg translator confuses scalars called
       x/y here with the luma `y` in main() and rejects the shader. */
    "  for(int tapX=0;tapX<2;tapX++)for(int tapY=0;tapY<2;tapY++){\n"
    "    vec2 at=uv+(vec2(float(tapX),float(tapY))-0.5)/512.0;\n"
    "    vec2 depthRG=h==0?texture2D(uHeadShadowMap0,at).rg:texture2D(uHeadShadowMap1,at).rg;\n"
    "    visibility+=step(depth,dot(depthRG,vec2(1.0,1.0/255.0)))*0.25;\n"
    "  }\n"
    "  return visibility;\n"
    "}\n"
    /* exp^2 distance fog: fades far batches into the sky colour (which is
       cleared to uFogColor, so the horizon and the haze always agree) */
    "void main(){\n"
    /* diagnostic UV visualization: bypasses lighting/texture/fog entirely.
       R=u, G=v (raw, unclamped -- a mesh whose UVs run outside [0,1] shows
       as a flat saturated patch instead of a gradient, exposing tiling
       regions at a glance) with a darkened 10x10 grid overlaid so uneven
       cell spacing reveals stretching and grid-line direction reveals
       flips/mirrors. Checked first so it overrides every other path. */
    "  if(uUVCheck>0.5){\n"
    "    vec2 g=abs(fract(vUV*10.0-0.5)-0.5);\n"
    "    float grid=smoothstep(0.0,0.04,min(g.x,g.y));\n"
    "    gl_FragColor=vec4(vec3(vUV.x,vUV.y,0.2)*mix(0.35,1.0,grid),1.0); return;\n"
    "  }\n"
    "  if(uCarFx>1.5){ gl_FragColor=uSH0+uSH1+uSH2+uSH3+uSH4+uSH5+uSH6+uSH7+uSH8+uSH9+uCDiffMin+uCDiffRange\n"
    "    +vec4(uCSpecMin+uCSpecRange+uCPow+uCEnvMin+uCEnvRange,0.0); return; }\n"
    /* world.fx linear fog: clamp((250 - viewZ) / (250 - start), floor, 1), with
       uFogDensity = 1/(250-start) and uFogK = start*uFogDensity; density 0 = none */
    "  float fd = vDepth*uFogDensity;\n"
    "  float fog = clamp(1.0 - fd + uFogK, uFogFloor, 1.0);\n"
    /* M132-R2 vista path. The backdrops are authored as cut-out sheets: their
       DXT1 blocks carry one-bit transparency (up to 57.7% of texels fully
       clear) and their DXT3 blocks carry real gradients, all of which the
       decoder used to throw away -- which is exactly why a panorama rendered
       as an opaque black-edged slab. Alpha here comes only from those two
       proven sources, texture alpha and source vertex-colour alpha; nothing is
       keyed off black pixels. Fully clear texels are discarded so they cannot
       write depth-adjacent artefacts or fringe. */
    "  if(uVista>0.5){\n"
    "    vec4 t = texture2D(uTex, vUV);\n"
    "    vec3 c = uUseTex>0.5 ? t.rgb : uColor;\n"
    "    float a = (uUseTex>0.5 ? t.a : 1.0) * vColor.a;\n"
    "    if(a < 0.02) discard;\n"
    "    gl_FragColor = vec4(mix(uFogColor, c, fog), a);\n"
    "    return;\n"
    "  }\n"
    "  if(uEmissiveTex>0.5){\n"
    "    vec4 t=texture2D(uTex,vUV);\n"
    "    float a=t.a*uAlpha; if(a<0.02) discard;\n"
    "    gl_FragColor=vec4(mix(uFogColor,t.rgb*uColor,fog),a); return;\n"
    "  }\n"
    "  if(uUnlit>0.5){ float a=uAlpha;\n"
    /* Shadow footprints need a filled rounded rectangle; lamp/neon sprites
       retain their radial falloff. Both fade to zero at the quad boundary. */
    "    if(uSoft>0.5){\n"
    "      if(uSoft>1.5){ float d=length(max(abs(vUV-vec2(0.5))-vec2(0.4),vec2(0.0))); a*=1.0-smoothstep(0.02,0.1,d); }\n"
    "      else { float d=length(vUV-vec2(0.5)); a*=clamp(1.0-d*2.0,0.0,1.0); } a*=a; }\n"
    "    gl_FragColor=vec4(mix(uFogColor,uColor,fog),a); return; }\n"
    /* Positions, normals, light and camera share world space. The model
       includes road tilt and each wheel/brake hub's own transform. */
    "  vec3 L=normalize(uLight); vec3 N=normalize(vN);\n"
    "  if(uFlipN>0.5) N = -N;\n"
    "  vec3 V=normalize(uCamPos - vPos);\n"
    /* Two-sided glass reflects the viewer-facing side; a backwards normal
       otherwise forces Fresnel to 1 and makes the far window fully opaque. */
    "  if(uFresnel>0.5 && dot(N,V)<0.0) N=-N;\n"
    "  float nl=max(dot(N,L),0.0);\n"
    "  float d=uAmbient+uDiffuse*nl;\n"   /* directional; reveals body form */
    /* uAmbient/uDiffuse are one shared per-frame pair (world+cars both read
       them), so retuning the defaults to fix a car panel would also retune
       every building and road. Cars get their own lower ambient floor /
       sharper diffuse swing right here instead, gated the same way as the
       cavity term above — world batches always set uSpec=0, so they never
       take this branch. */
    /* Paint was clipping to white: at 0.55/1.2 the diffuse term alone reached
       ~0.95, which the shared *1.35 exposure below pushed past 1.0 before the
       specular, rim and environment terms were even added -- so every panel
       saturated and the highlights had no headroom left to show against.
       Pulling the body's own diffuse back leaves that headroom for them. */
    "  if(uSpec>0.001) d=uAmbient*0.35+uDiffuse*0.80*nl;\n"
    /* uDecal: paint under an alpha-masked decal atlas (badges/vinyls) —
       texture RGB shows only where its alpha says so, paint elsewhere */
    "  vec4 t = texture2D(uTex,vUV);\n"
    /* N2_DRAW_CUTOUT world batches and authored wheel textures. Threshold 0.5
       matches the authored railing/fence texture's own 1-bit DXT1 alpha
       (fully 0 or 255, no partial value to tune against). */
    "  if(uAlphaTest>0.5 && t.a<0.5) discard;\n"
    "  vec3 base = uUseTex>0.5 ? (uDecal>0.5 ? mix(uColor,t.rgb,t.a) : t.rgb) : uColor;\n"
    /* Rim paint: recolor the diffuse toward uColor while KEEPING its detail.
       Luminance drives the shade, uColor picks the metal, so a gold OEM sheet
       becomes any painted colour (silver by default) with spokes/shadows
       intact. uRimTint 0 = raw OEM texture, 1 = fully painted. */
    "  if(uRimTint>0.001){ float luma=dot(t.rgb, vec3(0.299,0.587,0.114));\n"
    "    base = mix(base, uColor*(0.25+1.5*luma), uRimTint); }\n"
    /* cheap view-angle cavity darkening (cars only, uSpec>0 — the world
       batches always set uSpec=0): panel gaps/creases have no texture data
       to show them (verified: body meshes carry no diffuse map at all), so
       this fakes the early-2000s baked-AO look by darkening paint where the
       surface turns away from the camera, instead of claiming detail that
       isn't in the asset. Narrowed from pow4/0.6 to pow6/0.78: at pow4 the
       falloff reached far onto flat panels and fought the fresnel edge
       brightening below, which is what flattened the paint into matte clay.
       It now only bites in the last few degrees, i.e. actual creases. */
    "  if(uSpec>0.001){\n"
    "    float edge=pow(1.0-clamp(dot(N,V),0.0,1.0), 6.0);\n"
    "    base *= mix(1.0, 0.78, edge);\n"
    "  }\n"
    /* Phong: reflect the light about the normal and test it against the VIEW
       vector. The old form used dot(N,L) with no V term at all, so it was a
       sharpened diffuse -- the highlight could not travel across a panel as
       the camera moved, which is what made the paint read flat/matte. */
    "  float rl = max(dot(reflect(-L,N), V), 0.0);\n"
    "  float sp = pow(rl, uGloss)*uSpec;\n"
    /* clear coat: automotive paint is a coloured base coat under a clear
       lacquer, so it carries TWO highlights -- the broad soft one from the
       pigment (sp above) and a small hard one from the lacquer surface. One
       lobe alone is what makes painted metal read as moulded plastic. */
    "  sp += pow(rl, 160.0)*uClearcoat;\n"
    "  float rim = pow(1.0-abs(N.z), 3.0)*uSpec*0.4;\n"        /* fresnel-ish edge sheen */
    "  vec3 lit = base*d*1.35 + sp + rim;\n"
    /* World prelight uses MODULATE2X (0.5 neutral); textureless vehicle assets
       use the same byte slot as direct diffuse color for windows and trim. */
    "  if(uVColor>1.5) lit *= vColor.rgb;\n"
    "  else if(uVColor>0.001) lit *= clamp(vColor.rgb*2.0, 0.0, 1.6);\n"
    /* Surface lighting and per-lamp world-geometry shadows. */
    "  if(uHeadGain>0.0) for(int h=0;h<2;h++){\n"
    "    vec4 headPos=h==0?uHeadPos[0]:uHeadPos[1];\n"   /* constant indices, see headVisibility */
    "    vec3 ray=vPos-headPos.xyz; float dist=length(ray);\n"
    "    float along=dot(ray,uHeadForward);\n"
    "    if(headPos.w>0.5 && along>0.05 && dist<uHeadShape.w){\n"
    "      float lateral=dot(ray,uHeadRight)/along;\n"
    "      float elevation=dot(ray,uHeadUp)/along;\n"
    "      vec2 cone=vec2(lateral/uHeadShape.x,(elevation+uHeadShape.z)/uHeadShape.y);\n"
    "      float edge=1.0-smoothstep(0.55,1.0,length(cone));\n"
    "      float cutoff=mix(1.0,1.0-smoothstep(-0.018,-0.008,elevation),uHeadLow);\n"
    "      float reach=1.0-smoothstep(uHeadShape.w*0.65,uHeadShape.w,dist);\n"
    "      float lambert=max(dot(N,-ray/max(dist,0.001)),0.0);\n"
    "      if(edge*cutoff*lambert>0.0)lit+=base*uHeadColor*edge*cutoff*reach*lambert*uHeadGain*5.0*headVisibility(h,vPos,N,along)/(1.0+dist*dist/350.0);\n"
    "    }\n"
    "  }\n"
    /* environment reflection (cars only, uEnv>0): a procedural night-city
       sphere — dark ground, warm city-glow horizon band, dim blue sky —
       sampled with the world-space reflection vector, fresnel-weighted.
       The horizon stays level when the car banks or a wheel spins. */
    "  float fres = 0.35 + 0.65*pow(1.0-clamp(dot(N,V),0.0,1.0), 3.0);\n"
    "  if(uEnv>0.001){\n"
    "    vec3 R = reflect(-V, N);\n"
    "    float up = clamp(R.z*0.5+0.5, 0.0, 1.0);\n"
    /* stronger, wider night-city sphere: brighter sky dome and a much broader
       warm horizon band (pow 8 -> 4). The old values topped out at 0.11, so
       even at uEnv 0.5 the reflection never rose above the paint and the body
       read as plastic. This gives the clear-coat something bright enough to
       actually mirror. */
    "    vec3 env = mix(vec3(0.03,0.03,0.05), vec3(0.10,0.14,0.24), up)\n"
    "             + vec3(0.85,0.66,0.42)*pow(1.0-abs(R.z), 4.0);\n"
    "    lit += env * (uEnv * fres);\n"
    "  }\n"
    /* lit alpha = uAlpha (1 everywhere but the blended glass pass), so
       translucent glass keeps its specular highlight. M135-R: authored
       BLEND/ADD world batches and blended wheels multiply in the TEXTURE's own alpha
       (uTextureAlpha>0.5) -- otherwise a cutout-shaped blend/additive
       texture (e.g. a lit-window sheet with transparent gaps) would draw
       fully opaque/full-strength through those gaps, since uAlpha alone
       carries no per-texel information. */
    /* Lamp emission. Added after every lighting term so a lit lens keeps its
       highlight, rim sheen and environment reflection and still reads as
       switched on. The dome weight is the same dot(N,V) the fresnel uses: a
       curved lens is hottest where its surface faces the camera and falls off
       toward the rim, which is the difference between a lens and a sticker.
       The 0.55 floor keeps the lamp clearly lit all over. */
    "  if(dot(uEmissive,uEmissive)>0.0) lit += uEmissive*(0.55+0.45*clamp(dot(N,V),0.0,1.0))*(uUseTex>0.5?clamp(2.0*dot(t.rgb,vec3(0.299,0.587,0.114)),0.0,1.0):1.0);\n"
    "  float outA = uTextureAlpha>0.5 ? t.a*uAlpha : uAlpha;\n"
    /* Glass (uFresnel, the car glass pass only). A flat per-pass alpha is what
       made the cabin read as a still pool: every window sat at one constant
       opacity regardless of angle, so it looked like a filled surface rather
       than a pane. Real glass is nearly clear head-on and a mirror at grazing
       incidence, so alpha rides the same fresnel term the reflection does. */
    "  if(uFresnel>0.5) outA = clamp(mix(uAlpha, 1.0, fres), 0.0, 1.0);\n"
    "  gl_FragColor=vec4(mix(uFogColor, lit, fog), " DOF_ALPHA("outA") ");\n"
    "}\n";

/* ---- tiny 4x4 matrix (column-major) ---- */
void mat_mul(const float *a, const float *b, float *o) {
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a[k*4+j]*b[i*4+k]; o[i*4+j] = s;
    }
}
void mat_persp(float f, float ar, float n, float fa, float *m) {
    float t = 1.0f/tanf(f/2);
    memset(m, 0, 16*sizeof(float));
    m[0]=t/ar; m[5]=t; m[10]=(fa+n)/(n-fa); m[11]=-1; m[14]=2*fa*n/(n-fa);
}
static void vnorm(float *v){ float l=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(l<1e-6f)l=1;
    v[0]/=l; v[1]/=l; v[2]/=l; }
void mat_trans(float x,float y,float z,float *m){
    float r[16]={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1}; memcpy(m,r,sizeof r); }
void mat_rotz(float a, float *m){ float c=cosf(a),s=sinf(a);
    float r[16]={c,s,0,0, -s,c,0,0, 0,0,1,0, 0,0,0,1}; memcpy(m,r,sizeof r); }
/* Car model matrix: local +X = forward (heading), +Z = up (the ground normal),
   translated to pos + rideh along up. Forward is projected onto the plane
   perpendicular to up so the chassis banks with the road instead of staying
   level. up must be unit; a flat up=(0,0,1) reproduces mat_trans*mat_rotz. */
void mat_car(const float *pos, float heading, const float *up, float rideh, float *m){
    float f[3]={cosf(heading),sinf(heading),0};
    float d=f[0]*up[0]+f[1]*up[1]+f[2]*up[2];
    f[0]-=d*up[0]; f[1]-=d*up[1]; f[2]-=d*up[2]; vnorm(f);   /* forward in the plane */
    float l[3]={up[1]*f[2]-up[2]*f[1], up[2]*f[0]-up[0]*f[2], up[0]*f[1]-up[1]*f[0]}; /* left = up x fwd */
    float r[16]={ f[0],f[1],f[2],0,  l[0],l[1],l[2],0,  up[0],up[1],up[2],0,
        pos[0]+up[0]*rideh, pos[1]+up[1]*rideh, pos[2]+up[2]*rideh, 1 };
    memcpy(m,r,sizeof r);
}
void mat_car_footprint(const float pos[3],float heading,const float up[3],
                       const float bb[6],float length_scale,float width_scale,
                       float lift,float m[16]) {
    mat_car(pos,heading,up,lift,m);
    float sx=fmaxf(.5f,bb[3]-bb[0])*length_scale;
    float sy=fmaxf(.5f,bb[4]-bb[1])*width_scale;
    float x=(bb[0]+bb[3]-sx)*.5f,y=(bb[1]+bb[4]-sy)*.5f;
    for(int a=0;a<3;a++) {
        m[12+a]+=m[a]*x+m[4+a]*y;
        m[a]*=sx;m[4+a]*=sy;
    }
}

/* right-handed lookAt, column-major, up = world +Z */
void mat_lookat(const float *eye, const float *fwd, float *m) {
    float f[3]={fwd[0],fwd[1],fwd[2]}; vnorm(f);
    float up[3]={0,0,1};
    float s[3]={ f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0] }; vnorm(s);
    float u[3]={ s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0] };
    float r[16]={ s[0],u[0],-f[0],0,  s[1],u[1],-f[1],0,  s[2],u[2],-f[2],0,
        -(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]),
        -(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]),
          f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2], 1 };
    memcpy(m, r, sizeof r);
}

/* minimal PNG writer (dev screenshot). rgb is top-left origin, w*h*3. */
static void png_chunk(FILE *f, const char *tag, const unsigned char *d, uLong n) {
    unsigned char len[4] = { n>>24, n>>16, n>>8, n };
    fwrite(len, 1, 4, f);
    uLong crc = crc32(0, (const Bytef*)tag, 4);
    crc = crc32(crc, d, n);
    fwrite(tag, 1, 4, f); fwrite(d, 1, n, f);
    unsigned char c[4] = { crc>>24, crc>>16, crc>>8, crc };
    fwrite(c, 1, 4, f);
}
void write_png(const char *path, int w, int h, const unsigned char *rgb) {
    /* raw scanlines with filter byte 0 */
    uLong raw_n = (uLong)h * (1 + w*3);
    unsigned char *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        raw[y*(1+w*3)] = 0;
        memcpy(raw + y*(1+w*3) + 1, rgb + (size_t)y*w*3, w*3);
    }
    uLong comp_n = compressBound(raw_n);
    unsigned char *comp = malloc(comp_n);
    compress2(comp, &comp_n, raw, raw_n, 9);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        unsigned char ihdr[13] = { w>>24,w>>16,w>>8,w, h>>24,h>>16,h>>8,h, 8,2,0,0,0 };
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", comp, comp_n);
        png_chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(raw); free(comp);
}

#ifdef __vita__
static int g_shader_failed;
#endif
static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log);
        fprintf(stderr, "shader: %s\n", log);
#ifdef __vita__
        /* vitaGL dereferences a failed shader inside glLinkProgram and takes
           the process down. Record it and link a flat magenta stand-in so the
           failure is visible on screen and every other shader still loads. */
        vita_shader_failed(type, src, log);
        g_shader_failed = 1;
        glDeleteShader(s);
        static const char *fallback_vs = GLSL_HEADER
            "attribute vec3 aPos;uniform mat4 uMVP;"
            "void main(){gl_Position=uMVP*vec4(aPos,1.0);}";
        static const char *fallback_fs = GLSL_HEADER
            "void main(){gl_FragColor=vec4(1.0,0.0,1.0,1.0);}";
        s = glCreateShader(type);
        const char *fb = type == GL_VERTEX_SHADER ? fallback_vs : fallback_fs;
        glShaderSource(s, 1, &fb, NULL); glCompileShader(s);
#endif
    }
    return s;
}

#ifdef __vita__
/* Linked-program cache. vitaShaRK compiles GLSL on the device (the ten shader
   variants alone took 8.3 s), so a linked program is saved with
   glGetProgramBinary under a hash of its sources and restored with
   glProgramBinary on later boots. The binary carries the attribute bindings.
   Editing a shader changes the hash, so stale entries are simply never read. */
#include <sys/stat.h>
#define PROG_CACHE_MAX (256 * 1024)
/* programs compiled this boot (cache misses) and how many of them were saved:
   main.c restarts the game after a first-run compile (see shader_restart) */
int g_prog_compiles, g_prog_saved;
static GLuint program_cached(const char *vs, const char *fs) {
    uint32_t h = 2166136261u;
    for (const char *s = vs; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    h = (h ^ 0xff) * 16777619u;
    for (const char *s = fs; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    /* binaries are per vitaGL build: the patched one gets its own names */
    extern int vgl_ug2_build __attribute__((weak));
    char path[96];
    snprintf(path, sizeof path, "ux0:data/NFSU2/cache/prog_%s%08x.bin",
             &vgl_ug2_build ? "u_" : "", (unsigned)h);
    GLuint p = glCreateProgram();
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        void *buf = len > 0 && len <= PROG_CACHE_MAX ? malloc((size_t)len) : NULL;
        int got = buf && fread(buf, 1, (size_t)len, f) == (size_t)len;
        fclose(f);
        if (got) {
            glProgramBinary(p, 0, buf, (GLsizei)len);
            free(buf);
            GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (ok) return p;
            printf("shader cache: %s unusable, recompiling\n", path);
            glDeleteProgram(p); p = glCreateProgram();
        } else free(buf);
    }
    g_prog_compiles++;
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUV");
    glBindAttribLocation(p, 2, "aNor");
    glBindAttribLocation(p, 3, "aColor");
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok || g_shader_failed) return p;   /* never cache a magenta stand-in */
    void *buf = malloc(PROG_CACHE_MAX);
    GLsizei len = 0; GLenum fmt = 0;
    if (buf) glGetProgramBinary(p, PROG_CACHE_MAX, &len, &fmt, buf);
    mkdir("ux0:data/NFSU2/cache", 0777);   /* usually exists already */
    if (buf && len > 0 && len <= PROG_CACHE_MAX && (f = fopen(path, "wb"))) {
        if (fwrite(buf, 1, (size_t)len, f) == (size_t)len) g_prog_saved++;
        fclose(f);
    }
    free(buf);
    return p;
}
#endif

/* Colour maps plus a depth renderbuffer work without depth-texture extensions.
 * ponytail: fixed 512px world-only maps; add vehicle casters when their renderer
 * supports receiving world-space lights. Alpha-blended surfaces do not cast. */
void free_headlight_shadows(HeadlightShadows *s) {
    glDeleteTextures(2,s->texture);glDeleteTextures(1,&s->white);
    glDeleteRenderbuffers(1,&s->depth);glDeleteFramebuffers(1,&s->fbo);
    if(s->program)glDeleteProgram(s->program);
    memset(s,0,sizeof *s);
}

static int headlight_shadow_init(HeadlightShadows *s) {
    const char *vs=GLSL_VS_HEADER
        "attribute vec3 aPos;attribute vec2 aUV;uniform mat4 uMVP;"
        "varying vec2 uv;varying float vShadowDist;"
        "void main(){uv=aUV;gl_Position=uMVP*vec4(aPos,1.0);vShadowDist=gl_Position.w;}";
    const char *fs=GLSL_HEADER
        "uniform sampler2D uTex;uniform float uCutout,uRange;"
        "varying vec2 uv;varying float vShadowDist;"
        "void main(){if(uCutout>0.5 && texture2D(uTex,uv).a<0.5)discard;"
        "vec2 enc=fract(clamp(vShadowDist/uRange,0.0,0.99999)*vec2(1.0,255.0));"
        "enc.x-=enc.y/255.0;gl_FragColor=vec4(enc,0.0,1.0);}";
    GLuint v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
    s->program=glCreateProgram();glAttachShader(s->program,v);glAttachShader(s->program,f);
    glBindAttribLocation(s->program,0,"aPos");glBindAttribLocation(s->program,1,"aUV");
    glLinkProgram(s->program);glDeleteShader(v);glDeleteShader(f);
    GLint linked=0;glGetProgramiv(s->program,GL_LINK_STATUS,&linked);if(!linked)return 0;
    s->mvp=glGetUniformLocation(s->program,"uMVP");
    s->cutout=glGetUniformLocation(s->program,"uCutout");
    s->range=glGetUniformLocation(s->program,"uRange");
    glGenTextures(2,s->texture);
    for(int h=0;h<2;h++) {
        glBindTexture(GL_TEXTURE_2D,s->texture[h]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,512,512,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    unsigned char white[]={255,255,255};N2Tex tex={.w=1,.h=1,.rgb=white};s->white=upload_tex(&tex);
    glGenFramebuffers(1,&s->fbo);glBindFramebuffer(GL_FRAMEBUFFER,s->fbo);
    glGenRenderbuffers(1,&s->depth);glBindRenderbuffer(GL_RENDERBUFFER,s->depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,512,512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,s->depth);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s->texture[0],0);
    return s->white && glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
}

static int headlight_caster_visible(const N2Batch *b,const float m[16]) {
    for(int axis=0;axis<3;axis++)for(int sign=-1;sign<=1;sign+=2) {
        float plane[4];for(int a=0;a<4;a++)plane[a]=m[a*4+3]+sign*m[a*4+axis];
        float best=plane[3];
        for(int a=0;a<3;a++)best+=plane[a]*(plane[a]>=0?b->bbox_max[a]:b->bbox_min[a]);
        if(best<0)return 0;
    }
    return 1;
}

int render_headlight_shadows(const RProg *r,HeadlightShadows *s,const N2Batch *batches,int count) {
    float gain;glGetUniformfv(r->prog,r->uHeadGain,&gain);
    glUniform1f(r->uHeadShadow,0.0f);
    if(gain<=0 || s->failed)return s->failed?-1:0;
    float pos[2][4],forward[3],right[3],up[3],shape[4];
    /* glGetUniformfv returns one array element, not the whole array. */
    glGetUniformfv(r->prog,r->uHeadPos,pos[0]);
    glGetUniformfv(r->prog,glGetUniformLocation(r->prog,"uHeadPos[1]"),pos[1]);
    glGetUniformfv(r->prog,r->uHeadForward,forward);glGetUniformfv(r->prog,r->uHeadRight,right);
    glGetUniformfv(r->prog,r->uHeadUp,up);glGetUniformfv(r->prog,r->uHeadShape,shape);
    GLint program,fbo,rb,viewport[4],active,texture,depthfunc;
    GLfloat clear[4],clear_depth;GLboolean mask[4],depthmask;
    GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),dither=glIsEnabled(GL_DITHER);
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&rb);glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_DEPTH_FUNC,&depthfunc);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&depthmask);glGetFloatv(GL_DEPTH_CLEAR_VALUE,&clear_depth);
    int draws=0;float matrices[2][16];
    if(!s->fbo && !headlight_shadow_init(s)) {
        free_headlight_shadows(s);s->failed=1;
        fprintf(stderr,"Headlight shadows unavailable; retaining unshadowed beams.\n");
        draws=-1;goto restore;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,s->fbo);glViewport(0,0,512,512);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
#ifdef N2_GLES
    glClearDepthf(1.0f);
#else
    glClearDepth(1.0);
#endif
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(1,1,1,1);
    glUseProgram(s->program);glUniform1f(s->range,shape[3]);
    for(int h=0;h<2;h++) {
        float dir[3],vertical[3],view[16]={0},projection[16];
        for(int a=0;a<3;a++){dir[a]=forward[a]-up[a]*shape[2];vertical[a]=up[a]+forward[a]*shape[2];}
        vnorm(dir);vnorm(vertical);view[15]=1;
        for(int a=0;a<3;a++) {
            view[a*4]=right[a];view[a*4+1]=vertical[a];view[a*4+2]=-dir[a];
            view[12]-=right[a]*pos[h][a];view[13]-=vertical[a]*pos[h][a];view[14]+=dir[a]*pos[h][a];
        }
        mat_persp(2*atanf(shape[1]*1.15f),shape[0]/shape[1],.05f,shape[3],projection);
        mat_mul(projection,view,matrices[h]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s->texture[h],0);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(pos[h][3]<.5f)continue;
        glUniformMatrix4fv(s->mvp,1,GL_FALSE,matrices[h]);
        for(int i=0;i<count;i++) {
            const N2Batch *b=batches+i;
            if(b->drawmode!=N2_DRAW_OPAQUE && b->drawmode!=N2_DRAW_CUTOUT)continue;
            if(!headlight_caster_visible(b,matrices[h]))continue;
            glBindTexture(GL_TEXTURE_2D,b->tex?b->tex:s->white);
            glUniform1f(s->cutout,b->tex && b->drawmode==N2_DRAW_CUTOUT?1.0f:0.0f);
            draw_batch(b);draws++;
        }
    }
restore:
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fbo);glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)rb);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glUseProgram((GLuint)program);
    glClearColor(clear[0],clear[1],clear[2],clear[3]);glColorMask(mask[0],mask[1],mask[2],mask[3]);
    glDepthFunc((GLenum)depthfunc);glDepthMask(depthmask);
#ifdef N2_GLES
    glClearDepthf(clear_depth);
#else
    glClearDepth(clear_depth);
#endif
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    if(dither)glEnable(GL_DITHER);else glDisable(GL_DITHER);
    glBindTexture(GL_TEXTURE_2D,(GLuint)texture);
    if(draws>=0) {
        glUniformMatrix4fv(r->uHeadShadowMVP,2,GL_FALSE,matrices[0]);
        /* Units 1 and 2 are reserved for the two maps; ordinary textures use 0. */
        for(int h=0;h<2;h++){glActiveTexture(GL_TEXTURE1+h);glBindTexture(GL_TEXTURE_2D,s->texture[h]);}
        glUniform1i(r->uHeadShadowMap[0],1);glUniform1i(r->uHeadShadowMap[1],2);
        glUniform1f(r->uHeadShadow,1.0f);
    }
    glActiveTexture((GLenum)active);return draws;
}

void render_headlights(const RProg *r, const float model[16], const float anchors[4][4],
                       int high, float pitch, float range, float gain) {
    float pos[2][4];
    for(int b=0;b<2;b++) {
        for(int a=0;a<3;a++)pos[b][a]=model[a]*anchors[b][0]+model[4+a]*anchors[b][1]+
                                    model[8+a]*anchors[b][2]+model[12+a];
        pos[b][3]=anchors[b][3];
    }
    glUniform4fv(r->uHeadPos,2,&pos[0][0]);
    glUniform3fv(r->uHeadForward,1,model);
    glUniform3fv(r->uHeadRight,1,model+4);
    glUniform3fv(r->uHeadUp,1,model+8);
    glUniform4f(r->uHeadShape,high?.20f:.38f,high?.095f:.12f,
                tanf(pitch*3.14159265f/180.0f),fmaxf(range,1.0f));
    glUniform1f(r->uHeadLow,high?0.0f:1.0f);
    glUniform1f(r->uHeadGain,fmaxf(gain,0.0f));
    glUniform1f(r->uHeadShadow,0.0f); /* maps must be refreshed for this pose */
}

void render_tail_lamp(const RProg *r,GLuint texture,int running,int braking,
                      int boost,float gain) {
    int on=running || braking,hot=braking || boost;
    float level=on?fmaxf(gain,0.0f):0.0f;
    glUniform1f(r->uUnlit,0.0f);
    glUniform3f(r->uColor,.17f,.020f,.016f);
    glUniform3f(r->uEmissive,(hot?1.00f:.62f)*level,
                            (hot?.16f:.05f)*level,(hot?.10f:.04f)*level);
    glUniform1f(r->uSpec,.26f);glUniform1f(r->uEnv,.16f);
    glUniform1f(r->uGloss,20.0f);glUniform1f(r->uClearcoat,0.0f);
    glUniform1f(r->uDecal,0.0f);glUniform1f(r->uUseTex,texture?1.0f:0.0f);
    if(texture)glBindTexture(GL_TEXTURE_2D,texture);
}

void render_model(const RProg *r,const float model[16]) {
    static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glUniformMatrix4fv(r->uModel,1,GL_FALSE,model?model:identity);
}

#ifdef __vita__
/* PS Vita world pass: the main FS above carries car paint, glass, lamps,
   headlight shadows and reflections, all of which the GXM compiler keeps in
   one fragment program. The world only needs texture x prelight x sun, fog
   and alpha test, so it is drawn with this program under the main program's
   uniform locations (vita_alias_program in vita/sdl_vita.c). */
static const char *FS_WORLD_LITE =
    GLSL_HEADER
    "varying vec2 vUV; varying vec3 vN; varying float vDepth; varying vec4 vColor;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uVColor, uUseTex, uUnlit, uAlpha, uAlphaTest, uTextureAlpha, uEmissiveTex;\n"
    "uniform float uAmbient, uDiffuse, uFogDensity, uFogK, uFogFloor, uDofW;\n"
    "uniform vec3 uColor, uLight, uFogColor;\n"
    "void main(){\n"
    "  float fd=vDepth*uFogDensity; float fog=clamp(1.0-fd+uFogK,uFogFloor,1.0);\n"
    "  if(uUnlit>0.5){ gl_FragColor=vec4(mix(uFogColor,uColor,fog),uAlpha); return; }\n"
    "  vec4 t=texture2D(uTex,vUV);\n"
    "  if(uEmissiveTex>0.5){ float ea=t.a*uAlpha; if(ea<0.02) discard;\n"
    "    gl_FragColor=vec4(mix(uFogColor,t.rgb*uColor,fog)," DOF_ALPHA("ea") "); return; }\n"
    "  if(uAlphaTest>0.5 && t.a<0.5) discard;\n"
    "  vec3 base=uUseTex>0.5?t.rgb:uColor;\n"
    "  vec3 lit;\n"
    "  if(uVColor>1.5) lit=base*vColor.rgb;\n"
    "  else if(uVColor>0.001) lit=clamp(base*vColor.rgb*2.0,0.0,1.0);\n"   /* world.fx: 2*tex*vcol */
    "  else { float nl=max(dot(normalize(vN),normalize(uLight)),0.0); lit=base*(uAmbient+uDiffuse*nl)*1.35; }\n"
    "  float outA=uTextureAlpha>0.5?t.a*uAlpha:uAlpha;\n"
    "  gl_FragColor=vec4(mix(uFogColor,lit,fog)," DOF_ALPHA("outA") ");\n"
    "}\n";

/* FS_WORLD_LITE for opaque and blended world draws (variant key 18): no
   discard anywhere, so the GPU keeps hidden-surface removal for them. The
   unlit and emissive paths stay on FS_WORLD_LITE (vu_select). */
static const char *FS_WORLD_LITE_OPAQUE =
    GLSL_HEADER
    "varying vec2 vUV; varying vec3 vN; varying float vDepth; varying vec4 vColor;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uVColor, uUseTex, uAlpha, uTextureAlpha;\n"
    "uniform float uAmbient, uDiffuse, uFogDensity, uFogK, uFogFloor, uDofW;\n"
    "uniform vec3 uColor, uLight, uFogColor;\n"
    "void main(){\n"
    "  float fd=vDepth*uFogDensity; float fog=clamp(1.0-fd+uFogK,uFogFloor,1.0);\n"
    "  vec4 t=texture2D(uTex,vUV);\n"
    "  vec3 base=uUseTex>0.5?t.rgb:uColor;\n"
    "  vec3 lit;\n"
    "  if(uVColor>1.5) lit=base*vColor.rgb;\n"
    "  else if(uVColor>0.001) lit=clamp(base*vColor.rgb*2.0,0.0,1.0);\n"   /* world.fx: 2*tex*vcol */
    "  else { float nl=max(dot(normalize(vN),normalize(uLight)),0.0); lit=base*(uAmbient+uDiffuse*nl)*1.35; }\n"
    "  float outA=uTextureAlpha>0.5?t.a*uAlpha:uAlpha;\n"
    "  gl_FragColor=vec4(mix(uFogColor,lit,fog)," DOF_ALPHA("outA") ");\n"
    "}\n";

/* World cutout pass (variant key 19): foliage, fences, rails. A discarding
   program gets no hidden-surface removal, so every covered pixel of every
   cutout layer is shaded; the GPU probe put this pass at ~5 ms of a ~16 ms
   city frame. Light and vertex colour go per vertex (the uniform branches
   and both normalizes leave the pixel), leaving the pixel a texture fetch,
   the alpha test and fog. World normals are flat per face, so the result
   matches FS_WORLD_LITE. */
static const char *VS_WORLD_LITE =
    GLSL_VS_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec3 aNor; attribute vec4 aColor;\n"
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "uniform float uVColor, uAmbient, uDiffuse; uniform vec3 uLight;\n"
    "varying vec2 vUV; varying float vDepth; varying vec3 vLit;\n"
    "void main(){ vUV=aUV;\n"
    "  vec3 N=normalize((uModel*vec4(aNor,0.0)).xyz);\n"
    "  float nl=max(dot(N,normalize(uLight)),0.0);\n"
    "  vec3 l=vec3((uAmbient+uDiffuse*nl)*1.35);\n"
    "  if(uVColor>1.5) l=aColor.rgb;\n"
    "  else if(uVColor>0.001) l=aColor.rgb*2.0;\n"   /* world.fx: 2*tex*vcol, saturated per pixel */
    "  vLit=l; gl_Position=uMVP*vec4(aPos,1.0); vDepth=gl_Position.w; }\n";
static const char *FS_WORLD_LITE_CUTOUT =
    GLSL_HEADER
    "varying vec2 vUV; varying float vDepth; varying vec3 vLit;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uUseTex, uAlpha, uFogDensity, uFogK, uFogFloor, uDofW;\n"
    "uniform vec3 uColor, uFogColor;\n"
    "void main(){\n"
    "  vec4 t=texture2D(uTex,vUV);\n"
    "  if(t.a<0.5) discard;\n"
    "  float fd=vDepth*uFogDensity; float fog=clamp(1.0-fd+uFogK,uFogFloor,1.0);\n"
    "  vec3 base=mix(uColor,t.rgb,uUseTex);\n"
    "  gl_FragColor=vec4(mix(uFogColor,clamp(base*vLit,0.0,1.0),fog)," DOF_ALPHA("uAlpha") ");\n"
    "}\n";

/* Wet-road variant (key 20) = worldreflect.fx from SPEED2.EXE:
     r0 = tex * vcol;  out = 2*r0 + ReflectedTex * r0.a
   with ReflectedTex sampled projectively at the pixel's own screen spot.
   Here the lit/prelit colour is FS_WORLD_LITE_OPAQUE's, and the reflection
   is added before fog as the original's per-vertex fog then blends both. */
static const char *VS_WORLD_REFL =
    GLSL_VS_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec3 aNor; attribute vec4 aColor;\n"
    "uniform mat4 uMVP; uniform mat4 uModel; varying vec2 vUV; varying vec3 vN; varying float vDepth;\n"
    "varying vec4 vColor; varying vec3 vRefl;\n"
    "void main(){ vUV=aUV; vN=(uModel*vec4(aNor,0.0)).xyz; vColor=aColor;\n"
    "  gl_Position=uMVP*vec4(aPos,1.0); vDepth=gl_Position.w;\n"
    "  vRefl=vec3(gl_Position.x+gl_Position.w, gl_Position.y+gl_Position.w, 2.0*gl_Position.w); }\n";
static const char *FS_WORLD_LITE_REFLECT =
    GLSL_HEADER
    "varying vec2 vUV; varying vec3 vN; varying float vDepth; varying vec4 vColor; varying vec3 vRefl;\n"
    "uniform sampler2D uTex; uniform sampler2D uReflTex;\n"
    "uniform float uVColor, uUseTex, uAlpha, uTextureAlpha;\n"
    "uniform float uAmbient, uDiffuse, uFogDensity, uFogK, uFogFloor, uDofW;\n"
    "uniform vec3 uColor, uLight, uFogColor;\n"
    "void main(){\n"
    "  float fd=vDepth*uFogDensity; float fog=clamp(1.0-fd+uFogK,uFogFloor,1.0);\n"
    "  vec4 t=texture2D(uTex,vUV);\n"
    "  vec3 base=uUseTex>0.5?t.rgb:uColor;\n"
    "  float wet=(uUseTex>0.5?t.a:1.0)*vColor.a;\n"
    "  vec3 lit=clamp(base*vColor.rgb*2.0+texture2DProj(uReflTex,vRefl).rgb*wet,0.0,1.0);\n"
    "  float outA=uTextureAlpha>0.5?t.a*uAlpha:uAlpha;\n"
    "  gl_FragColor=vec4(mix(uFogColor,lit,fog)," DOF_ALPHA("outA") ");\n"
    "}\n";

/* Sky dome (variant key 23): FS_WORLD_LITE's emissive path without its
   discard. The dome is drawn opaque first and buildings cover most of it
   downtown; with no discard in the program the GPU's hidden-surface removal
   skips every covered dome pixel instead of shading the whole screen. */
static const char *FS_SKY_OPAQUE =
    GLSL_HEADER
    "varying vec2 vUV; varying float vDepth;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlpha, uFogDensity, uFogK, uFogFloor, uDofW;\n"
    "uniform vec3 uColor, uFogColor;\n"
    "void main(){\n"
    "  float fd=vDepth*uFogDensity; float fog=clamp(1.0-fd+uFogK,uFogFloor,1.0);\n"
    "  vec4 t=texture2D(uTex,vUV);\n"
    "  gl_FragColor=vec4(mix(uFogColor,t.rgb*uColor,fog)," DOF_ALPHA("t.a*uAlpha") ");\n"
    "}\n";

/* world.fx fast path (variant key 24): the textured, prelit case that nearly
   every opaque and blended world draw takes (uUseTex 1, uVColor in (0,1.5]),
   sat(2*tex*vcol), with no uniform branches left in the pixel. Fog goes per
   vertex like the original's (linear in depth, so the interpolated value is
   the per-pixel one wherever the clamp is not active); uTextureAlpha is 0 or
   1, so the alpha select becomes a mix. */
static const char *VS_WORLD_FAST =
    GLSL_VS_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec4 aColor;\n"
    "uniform mat4 uMVP; uniform float uFogDensity, uFogK, uFogFloor;\n"
    "varying vec2 vUV; varying vec3 vCol; varying vec2 vFog;\n"
    "void main(){ vUV=aUV; vCol=aColor.rgb*2.0;\n"
    "  gl_Position=uMVP*vec4(aPos,1.0);\n"
    "  vFog=vec2(clamp(1.0-gl_Position.w*uFogDensity+uFogK,uFogFloor,1.0),\n"
    "            clamp(gl_Position.w*0.0009765625,0.0,1.0)); }\n";   /* y: DOF depth */
static const char *FS_WORLD_FAST =
    GLSL_HEADER
    "varying vec2 vUV; varying vec3 vCol; varying vec2 vFog;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlpha, uTextureAlpha, uDofW;\n"
    "uniform vec3 uFogColor;\n"
    "void main(){\n"
    "  vec4 t=texture2D(uTex,vUV);\n"
    "  vec3 lit=clamp(t.rgb*vCol,0.0,1.0);\n"
    "  gl_FragColor=vec4(mix(uFogColor,lit,vFog.x),mix(uAlpha*mix(1.0,t.a,uTextureAlpha),vFog.y,uDofW));\n"
    "}\n";

/* Specialised variants of the main fragment program (PS Vita). The main FS
   steers every path (paint, glass, lamps, sprites, HUD) with uniforms, and
   the GXM compiler pays for all of it per pixel: the player car alone cost
   ~15 ms of GPU more than under the lite program. Each variant keeps one
   path with its features fixed at compile time and the same results:
     key 0-7  lit; bit 1 specular (crease, phong, clear coat, rim),
              bit 2 environment reflection, bit 4 glass fresnel
     key 10-17 the same with the alpha-test discard (only those can
              discard: a discard in the program costs every draw HSR)
     key 8    textured unlit (uEmissiveTex)
     key 9    flat unlit (uUnlit; shadows, halos, HUD fills)
   pow() with constant exponents becomes multiplies. The UV checker and
   vista paths stay on the main program. sdl_vita.c picks the variant per
   draw from the cached uniform values (vu_select). */
static const char *const g_variant_names[] = {
    "uMVP", "uModel", "uTex", "uVColor", "uUseTex", "uColor", "uUnlit", "uAlpha", "uSoft",
    "uSpec", "uDecal", "uAmbient", "uDiffuse", "uLight", "uFogColor", "uFogDensity",
    "uCamPos", "uEnv", "uUVCheck", "uGloss", "uFlipN", "uRimTint", "uEmissive", "uVista",
    "uAlphaTest", "uTextureAlpha", "uEmissiveTex", "uFresnel", "uClearcoat", "uFogK", "uFogFloor",
    "uCarFx", "uSH0", "uSH1", "uSH2", "uSH3", "uSH4", "uSH5", "uSH6", "uSH7", "uSH8", "uSH9",
    "uCDiffMin", "uCDiffRange", "uCSpecMin", "uCSpecRange", "uCPow", "uCEnvMin", "uCEnvRange",
    "uDofW" };

static void variant_fs(int key, char *s, size_t cap) {
    int cut = key >= 10 && key < 18, bits = cut ? key - 10 : key;
    int spec = bits < 8 && (bits & 1), env = bits < 8 && (bits & 2), fres = bits < 8 && (bits & 4);
    size_t n = 0;
#define VADD(str) do { size_t l_ = strlen(str); if (n + l_ < cap) { memcpy(s + n, str, l_); n += l_; } } while (0)
    VADD(GLSL_HEADER);
    VADD("varying vec2 vUV; varying vec3 vN; varying float vDepth; varying vec3 vPos; varying vec4 vColor;\n"
         "uniform sampler2D uTex;\n"
         "uniform float uVColor, uUseTex, uUnlit, uAlpha, uSoft, uSpec, uDecal, uAmbient, uDiffuse;\n"
         "uniform float uFogDensity, uFogK, uFogFloor, uEnv, uGloss, uFlipN, uRimTint, uAlphaTest, uTextureAlpha;\n"
         "uniform float uFresnel, uClearcoat, uDofW;\n"
         "uniform vec3 uColor, uLight, uFogColor, uCamPos, uEmissive;\n"
         "void main(){\n"
         "  float fd=vDepth*uFogDensity; float fog=clamp(1.0-fd+uFogK,uFogFloor,1.0);\n");
    if (key == 8) {
        VADD("  vec4 t=texture2D(uTex,vUV);\n"
             "  float a=t.a*uAlpha; if(a<0.02) discard;\n"
             "  gl_FragColor=vec4(mix(uFogColor,t.rgb*uColor,fog)," DOF_ALPHA("a") ");\n}\n");
    } else if (key == 9) {
        VADD("  float a=uAlpha;\n"
             "  if(uSoft>0.5){\n"
             "    if(uSoft>1.5){ float d=length(max(abs(vUV-vec2(0.5))-vec2(0.4),vec2(0.0))); a*=1.0-smoothstep(0.02,0.1,d); }\n"
             "    else { float d=length(vUV-vec2(0.5)); a*=clamp(1.0-d*2.0,0.0,1.0); } a*=a; }\n"
             "  gl_FragColor=vec4(mix(uFogColor,uColor,fog),a);\n}\n");
    } else {
        VADD("  vec3 L=normalize(uLight); vec3 N=normalize(vN);\n"
             "  if(uFlipN>0.5) N=-N;\n"
             "  vec3 V=normalize(uCamPos-vPos);\n");
        if (fres) VADD("  if(dot(N,V)<0.0) N=-N;\n");
        VADD("  float nl=max(dot(N,L),0.0);\n"
             "  float d=uAmbient+uDiffuse*nl;\n");
        if (spec) VADD("  if(uSpec>0.001) d=uAmbient*0.35+uDiffuse*0.80*nl;\n");
        VADD("  vec4 t=texture2D(uTex,vUV);\n");
        if (cut) VADD("  if(t.a<0.5) discard;\n");
        VADD("  vec3 base=uUseTex>0.5?(uDecal>0.5?mix(uColor,t.rgb,t.a):t.rgb):uColor;\n"
             "  if(uRimTint>0.001){ float luma=dot(t.rgb,vec3(0.299,0.587,0.114));\n"
             "    base=mix(base,uColor*(0.25+1.5*luma),uRimTint); }\n"
             "  float ndv=clamp(dot(N,V),0.0,1.0); float e1=1.0-ndv; float e3=e1*e1*e1;\n");
        if (spec) VADD("  if(uSpec>0.001) base*=mix(1.0,0.78,e3*e3);\n"
                       "  float rl=max(dot(reflect(-L,N),V),0.0);\n"
                       "  float sp=pow(rl,uGloss)*uSpec;\n"
                       "  float r2=rl*rl; float r4=r2*r2; float r8=r4*r4; float r16=r8*r8; float r32=r16*r16;\n"
                       "  float r128=r32*r32; r128*=r128;\n"
                       "  sp+=r128*r32*uClearcoat;\n"
                       "  float q=1.0-abs(N.z); float rim=q*q*q*uSpec*0.4;\n"
                       "  vec3 lit=base*d*1.35+sp+rim;\n");
        else VADD("  vec3 lit=base*d*1.35;\n");
        VADD("  if(uVColor>1.5) lit*=vColor.rgb;\n"
             "  else if(uVColor>0.001) lit*=clamp(vColor.rgb*2.0,0.0,1.6);\n");
        if (env || fres) VADD("  float fres=0.35+0.65*e3;\n");
        if (env) VADD("  vec3 R=reflect(-V,N);\n"
                      "  float up=clamp(R.z*0.5+0.5,0.0,1.0); float h=1.0-abs(R.z); h*=h;\n"
                      "  vec3 env=mix(vec3(0.03,0.03,0.05),vec3(0.10,0.14,0.24),up)+vec3(0.85,0.66,0.42)*(h*h);\n"
                      "  lit+=env*(uEnv*fres);\n");
        VADD("  if(dot(uEmissive,uEmissive)>0.0) lit+=uEmissive*(0.55+0.45*ndv);\n"
             "  float outA=uTextureAlpha>0.5?t.a*uAlpha:uAlpha;\n");
        if (fres) VADD("  outA=clamp(mix(uAlpha,1.0,fres),0.0,1.0);\n");
        VADD("  gl_FragColor=vec4(mix(uFogColor,lit,fog)," DOF_ALPHA("outA") ");\n}\n");
    }
#undef VADD
    s[n < cap ? n : cap - 1] = 0;
}

static void render_main_variants(void) {
    Uint64 t0 = SDL_GetPerformanceCounter();
    int made = 0;
    for (int key = 0; key < 25; key++) {
        static char fs[4096];
        if (key == 23 || key == 24) {   /* sky dome, world.fx fast path */
            GLuint p = key == 23 ? program_cached(VS, FS_SKY_OPAQUE)
                                 : program_cached(VS_WORLD_FAST, FS_WORLD_FAST);
            GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) { printf("shader variant %d: link failed, stays on the main program\n", key); continue; }
            vita_add_variant(p, key, g_variant_names, (int)(sizeof g_variant_names / sizeof *g_variant_names));
            made++;
            continue;
        }
        if (key >= 21)   /* car.fx; 22 keeps the alpha-test discard (cut-out rims) */
            snprintf(fs, sizeof fs, "%s%s%s", FS_CAR_HEAD,
                     key == 22 ? "  if(t.a<0.5) discard;\n" : "", FS_CAR_TAIL);
        else if (key == 18) snprintf(fs, sizeof fs, "%s", FS_WORLD_LITE_OPAQUE);
        else if (key == 19) snprintf(fs, sizeof fs, "%s", FS_WORLD_LITE_CUTOUT);
        else if (key == 20) snprintf(fs, sizeof fs, "%s", FS_WORLD_LITE_REFLECT);
        else variant_fs(key, fs, sizeof fs);
        GLuint p = program_cached(key >= 21 ? VS_CAR : key == 19 ? VS_WORLD_LITE
                                  : key == 20 ? VS_WORLD_REFL : VS, fs);
        if (key >= 21) {   /* the env atlas' sampler unit, set once */
            GLint cur = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
            glUseProgram(p);
            glUniform1i(glGetUniformLocation(p, "uEnvTex"), RENDER_ENV_UNIT);
            glUniform1i(glGetUniformLocation(p, "uTex"), 0);
            glUseProgram((GLuint)cur);
        }
        if (key == 20) {   /* the reflection's own sampler unit, set once */
            GLint cur = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
            glUseProgram(p);
            glUniform1i(glGetUniformLocation(p, "uReflTex"), RENDER_REFL_UNIT);
            glUniform1i(glGetUniformLocation(p, "uTex"), 0);
            glUseProgram((GLuint)cur);
        }
        GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) { printf("shader variant %d: link failed, stays on the main program\n", key); continue; }
        vita_add_variant(p, key, g_variant_names, (int)(sizeof g_variant_names / sizeof *g_variant_names));
        made++;
    }
    printf("shader variants: %d of 25 built in %.0f ms\n", made,
           (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
}

void render_world_lite_program(const RProg *main_prog) {
    static const char *const names[] = {
        "uMVP", "uModel", "uUseTex", "uColor", "uUnlit", "uAlpha", "uAlphaTest",
        "uTextureAlpha", "uFogColor", "uFogDensity", "uAmbient", "uDiffuse",
        "uLight", "uVColor", "uEmissiveTex" };
    GLuint p = program_cached(VS, FS_WORLD_LITE);
    /* the full name list: names the lite program lacks are dropped while it runs */
    (void)names;
    vita_alias_program(main_prog->prog, p, g_variant_names,
                       (int)(sizeof g_variant_names / sizeof *g_variant_names));
    render_main_variants();
    glUseProgram(main_prog->prog);
}
#endif

RProg render_program(void) {
    RProg r;
#ifndef __vita__
    r.prog = glCreateProgram();
    glAttachShader(r.prog, compile(GL_VERTEX_SHADER, VS));
#endif
#ifdef __vita__
    Uint64 prog_t0 = SDL_GetPerformanceCounter();
    {   /* cut "float headVisibility(" .. "void main(){" and the beam loop
           "  if(uHeadGain>0.0)" .. "  float fres =" out of FS */
        static char fs_vita[16384];
        const char *a0 = strstr(FS, "float headVisibility("), *a1 = strstr(FS, "void main(){");
        const char *b0 = strstr(FS, "  if(uHeadGain>0.0)"), *b1 = strstr(FS, "  float fres =");
        if (a0 && a1 && b0 && b1 && a0 < a1 && a1 < b0 && b0 < b1 &&
            strlen(FS) < sizeof fs_vita) {
            size_t n = 0;
            memcpy(fs_vita + n, FS, (size_t)(a0 - FS)); n += (size_t)(a0 - FS);
            memcpy(fs_vita + n, a1, (size_t)(b0 - a1)); n += (size_t)(b0 - a1);
            strcpy(fs_vita + n, b1);
            r.prog = program_cached(VS, fs_vita);
        } else
            r.prog = program_cached(VS, FS);
    }
    printf("main shader program ready in %.0f ms\n",
           (double)(SDL_GetPerformanceCounter() - prog_t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
#else
    glAttachShader(r.prog, compile(GL_FRAGMENT_SHADER, FS));
    glBindAttribLocation(r.prog, 0, "aPos");
    glBindAttribLocation(r.prog, 1, "aUV");
    glBindAttribLocation(r.prog, 2, "aNor");
    glBindAttribLocation(r.prog, 3, "aColor");
    glLinkProgram(r.prog);
#endif
    glUseProgram(r.prog);
    r.uMVP     = glGetUniformLocation(r.prog, "uMVP");
    r.uModel   = glGetUniformLocation(r.prog, "uModel");
    render_model(&r,NULL);
    r.uUseTex  = glGetUniformLocation(r.prog, "uUseTex");
    r.uColor   = glGetUniformLocation(r.prog, "uColor");
    r.uUnlit   = glGetUniformLocation(r.prog, "uUnlit");
    r.uAlpha   = glGetUniformLocation(r.prog, "uAlpha");
    r.uSoft    = glGetUniformLocation(r.prog, "uSoft");
    r.uSpec    = glGetUniformLocation(r.prog, "uSpec");
    r.uDecal   = glGetUniformLocation(r.prog, "uDecal");
    r.uVista      = glGetUniformLocation(r.prog, "uVista");
    r.uAlphaTest  = glGetUniformLocation(r.prog, "uAlphaTest");
    r.uTextureAlpha = glGetUniformLocation(r.prog, "uTextureAlpha");
    r.uEmissiveTex = glGetUniformLocation(r.prog, "uEmissiveTex");
    r.uFogColor   = glGetUniformLocation(r.prog, "uFogColor");
    r.uFogDensity = glGetUniformLocation(r.prog, "uFogDensity");
    r.uFogK = glGetUniformLocation(r.prog, "uFogK");
    r.uFogFloor = glGetUniformLocation(r.prog, "uFogFloor");
    r.uCamPos = glGetUniformLocation(r.prog, "uCamPos");
    r.uEnv    = glGetUniformLocation(r.prog, "uEnv");
    r.uUVCheck = glGetUniformLocation(r.prog, "uUVCheck");
    r.uAmbient = glGetUniformLocation(r.prog, "uAmbient");
    r.uDiffuse = glGetUniformLocation(r.prog, "uDiffuse");
    r.uLight   = glGetUniformLocation(r.prog, "uLight");
    r.uVColor  = glGetUniformLocation(r.prog, "uVColor");
    r.uGloss   = glGetUniformLocation(r.prog, "uGloss");
    r.uFlipN   = glGetUniformLocation(r.prog, "uFlipN");
    r.uRimTint = glGetUniformLocation(r.prog, "uRimTint");
    r.uEmissive = glGetUniformLocation(r.prog, "uEmissive");
    r.uFresnel = glGetUniformLocation(r.prog, "uFresnel");
    r.uClearcoat = glGetUniformLocation(r.prog, "uClearcoat");
    r.uHeadPos = glGetUniformLocation(r.prog, "uHeadPos[0]");
    r.uHeadForward = glGetUniformLocation(r.prog, "uHeadForward");
    r.uHeadRight = glGetUniformLocation(r.prog, "uHeadRight");
    r.uHeadUp = glGetUniformLocation(r.prog, "uHeadUp");
    r.uHeadShape = glGetUniformLocation(r.prog, "uHeadShape");
    r.uHeadGain = glGetUniformLocation(r.prog, "uHeadGain");
    r.uHeadLow = glGetUniformLocation(r.prog, "uHeadLow");
    r.uHeadColor = glGetUniformLocation(r.prog, "uHeadColor");
    r.uHeadShadow = glGetUniformLocation(r.prog, "uHeadShadow");
    r.uHeadShadowMap[0] = glGetUniformLocation(r.prog, "uHeadShadowMap0");
    r.uHeadShadowMap[1] = glGetUniformLocation(r.prog, "uHeadShadowMap1");
    r.uHeadShadowMVP = glGetUniformLocation(r.prog, "uHeadShadowMVP[0]");
    glUniform1i(r.uHeadShadowMap[0],0);glUniform1i(r.uHeadShadowMap[1],0);
    glUniform1f(r.uHeadShadow,0.0f);
    glUniform1f(r.uHeadGain,0.0f);
    glUniform1f(r.uAlpha, 1.0f); glUniform1f(r.uSoft, 0.0f); glUniform1f(r.uSpec, 0.0f);
    glUniform1f(r.uDecal, 0.0f); glUniform1f(r.uRimTint, 0.0f);
    glUniform3f(r.uEmissive, 0.0f, 0.0f, 0.0f);
    glUniform3f(r.uFogColor, 0.06f, 0.07f, 0.11f); glUniform1f(r.uFogDensity, 0.0f);
    glUniform1f(r.uFogK, 0.0f); glUniform1f(r.uFogFloor, 0.0f);
    glUniform3f(r.uCamPos, 0, 0, 0); glUniform1f(r.uEnv, 0.0f);
    glUniform1f(r.uUVCheck, 0.0f);
    glUniform1f(r.uGloss, 20.0f);
    glUniform1f(r.uFlipN, 0.0f);
    glUniform1f(r.uAlphaTest, 0.0f);
    glUniform1f(r.uTextureAlpha, 0.0f);
    glUniform1f(r.uEmissiveTex, 0.0f);
    glUniform3f(r.uLight, N2_SUN_X, N2_SUN_Y, N2_SUN_Z);
    return r;
}

/* Authored car normals preserve smoothing across material/UV seams. Generated
 * geometry and invalid source normals use area-weighted face accumulation;
 * nor must hold nverts*3 floats, zeroed by the caller. */
static void mesh_normals(const N2Mesh *m, float *nor) {
    for (int t = 0; t + 2 < m->nidx; t += 3) {
        int a=m->idx[t], b=m->idx[t+1], c=m->idx[t+2];
        float *pa=m->verts+a*5,*pb=m->verts+b*5,*pc=m->verts+c*5;
        float ux=pb[0]-pa[0],uy=pb[1]-pa[1],uz=pb[2]-pa[2];
        float vx=pc[0]-pa[0],vy=pc[1]-pa[1],vz=pc[2]-pa[2];
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        int ii[3]={a,b,c};
        for(int e=0;e<3;e++){ nor[ii[e]*3]+=nx; nor[ii[e]*3+1]+=ny; nor[ii[e]*3+2]+=nz; }
    }
    for (int v=0;v<m->nverts;v++){ float*np=nor+v*3;
        if(m->authored_normals) {
            const float *source=m->verts+m->nverts*5+v*3;
            float len2=source[0]*source[0]+source[1]*source[1]+source[2]*source[2];
            if(isfinite(len2) && len2>1e-12f)memcpy(np,source,3*sizeof(float));
        }
        float l=sqrtf(np[0]*np[0]+np[1]*np[1]+np[2]*np[2]); if(l<1e-6f)l=1;
        np[0]/=l; np[1]/=l; np[2]/=l; }
}

void free_scene_gpu(GpuMesh *gm, int count) {
    if (!gm) return;
    for (int i = 0; i < count; i++) {
        glDeleteBuffers(1, &gm[i].vbo);
        glDeleteBuffers(1, &gm[i].nbo);
        glDeleteBuffers(1, &gm[i].cbo);
        glDeleteBuffers(1, &gm[i].ibo);
    }
    free(gm);
}

/* Upload meshes with per-vertex normals; allocation failure releases partial buffers. */
GpuMesh *upload_scene(N2Scene *s) {
    GpuMesh *gm = (GpuMesh *)calloc(s->count, sizeof(GpuMesh));
    if (!gm) return NULL;
    for (int i = 0; i < s->count; i++) {
        N2Mesh *m = &s->meshes[i];
        if (m->world_role == 1) continue;
        N2Mesh rounded={0};
        if (n2_round_wheel_tyre(m,&rounded)) m=&rounded;
        float *nor = (float *)calloc(m->nverts * 3, sizeof(float));
        if (!nor) {
            free(rounded.verts); free(rounded.idx);
            free_scene_gpu(gm, s->count);
            return NULL;
        }
        mesh_normals(m, nor);
        glGenBuffers(1,&gm[i].vbo); glBindBuffer(GL_ARRAY_BUFFER,gm[i].vbo);
        glBufferData(GL_ARRAY_BUFFER, m->nverts*5*sizeof(float), m->verts, GL_STATIC_DRAW);
        glGenBuffers(1,&gm[i].nbo); glBindBuffer(GL_ARRAY_BUFFER,gm[i].nbo);
        glBufferData(GL_ARRAY_BUFFER, m->nverts*3*sizeof(float), nor, GL_STATIC_DRAW);
        if (m->vcol) {
            glGenBuffers(1,&gm[i].cbo); glBindBuffer(GL_ARRAY_BUFFER,gm[i].cbo);
            glBufferData(GL_ARRAY_BUFFER, m->nverts*4, m->vcol, GL_STATIC_DRAW);
        }
        glGenBuffers(1,&gm[i].ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gm[i].ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->nidx*sizeof(uint16_t), m->idx, GL_STATIC_DRAW);
        gm[i].nidx = m->nidx; gm[i].cat = m->cat; gm[i].texkey = m->texkey;
        gm[i].trim = m->trim; gm[i].draw_mode = m->draw_mode;
        gm[i].car_material = m->car_material; gm[i].win = m->car_win;
        free(nor);
        free(rounded.verts); free(rounded.idx);
    }
    return gm;
}

/* Draw merging for a car drawn as a whole (the AI cars): the meshes 'ok'
   marks that share every per-draw state (category, vertex colour, texture,
   material, trim) become one buffer set, drawn at the first one's place.
   lead[i] = merged index for a group's first mesh, -1 for the other
   members, -2 for meshes left alone. Same vertices, normals and colours as
   upload_scene, so the picture is the same with fewer draws. */
GpuMesh *upload_scene_merged(const N2Scene *s, const GpuMesh *gm, const unsigned char *ok,
                             short *lead, int *nout) {
    *nout = 0;
    for (int i = 0; i < s->count; i++) lead[i] = -2;
    GpuMesh *out = (GpuMesh *)calloc((size_t)s->count, sizeof(GpuMesh));
    int *members = (int *)malloc((size_t)s->count * sizeof(int));
    if (!out || !members) { free(out); free(members); return NULL; }
    int n = 0;
    for (int i = 0; i < s->count; i++) {
        if (!ok[i] || lead[i] != -2 || s->meshes[i].nverts > 65535) continue;
        const N2Mesh *a = &s->meshes[i];
        int nm = 0, nv = 0, ni = 0;
        for (int j = i; j < s->count; j++) {
            if (!ok[j] || lead[j] != -2) continue;
            const N2Mesh *b = &s->meshes[j];
            if (gm[j].cat != gm[i].cat || !b->vcol != !a->vcol || gm[j].texkey != gm[i].texkey ||
                gm[j].car_material != gm[i].car_material || gm[j].trim != gm[i].trim ||
                gm[j].draw_mode != gm[i].draw_mode) continue;
            if (nv + b->nverts > 65535) continue;          /* 16-bit indices */
            members[nm++] = j; nv += b->nverts; ni += b->nidx;
            lead[j] = -1;
        }
        if (nm == 1) {
            lead[i] = -2;
            continue;
        }
        lead[i] = (short)n;
        float *pos = (float *)malloc((size_t)nv * 5 * sizeof(float));
        float *nor = (float *)calloc((size_t)nv * 3, sizeof(float));
        uint16_t *idx = (uint16_t *)malloc((size_t)ni * sizeof(uint16_t));
        unsigned char *col = a->vcol ? (unsigned char *)malloc((size_t)nv * 4) : NULL;
        if (!pos || !nor || !idx || (a->vcol && !col)) {
            free(pos); free(nor); free(idx); free(col);
            for (int q = 0; q < nm; q++) lead[members[q]] = -2;   /* drawn alone */
            continue;
        }
        int vo = 0, io = 0;
        for (int q = 0; q < nm; q++) {
            const N2Mesh *b = &s->meshes[members[q]];
            memcpy(pos + 5 * vo, b->verts, (size_t)b->nverts * 5 * sizeof(float));
            mesh_normals(b, nor + 3 * vo);
            if (col) memcpy(col + 4 * vo, b->vcol, (size_t)b->nverts * 4);
            for (int t = 0; t < b->nidx; t++) idx[io + t] = (uint16_t)(b->idx[t] + vo);
            vo += b->nverts; io += b->nidx;
        }
        GpuMesh *g = &out[n];
        glGenBuffers(1, &g->vbo); glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * 5 * sizeof(float), pos, GL_STATIC_DRAW);
        glGenBuffers(1, &g->nbo); glBindBuffer(GL_ARRAY_BUFFER, g->nbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * 3 * sizeof(float), nor, GL_STATIC_DRAW);
        if (col) {
            glGenBuffers(1, &g->cbo); glBindBuffer(GL_ARRAY_BUFFER, g->cbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * 4, col, GL_STATIC_DRAW);
        }
        glGenBuffers(1, &g->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ni * sizeof(uint16_t), idx, GL_STATIC_DRAW);
        g->nidx = ni; g->cat = gm[i].cat; g->texkey = gm[i].texkey; g->trim = gm[i].trim;
        g->draw_mode = gm[i].draw_mode; g->car_material = gm[i].car_material; g->win = gm[i].win;
        free(pos); free(nor); free(idx); free(col);
        n++;
    }
    free(members);
    *nout = n;
    return out;
}

void render_mesh_repose(GpuMesh *g, const N2Mesh *m, const float *M) {
    if (!g || !m || !g->vbo || !g->nbo || m->nverts <= 0 || !m->verts) return;
    float *nor = (float *)calloc((size_t)m->nverts * 3, sizeof(float));
    float *pos = M ? (float *)malloc((size_t)m->nverts * 5 * sizeof(float)) : NULL;
    if (!nor || (M && !pos)) { free(nor); free(pos); return; }
    mesh_normals(m, nor);
    if (M) {
        for (int v = 0; v < m->nverts; v++) {
            const float *p = m->verts + 5 * v; float *o = pos + 5 * v, *n = nor + 3 * v;
            float x = p[0], y = p[1], z = p[2], nx = n[0], ny = n[1], nz = n[2];
            o[0] = x * M[0] + y * M[4] + z * M[8] + M[12];
            o[1] = x * M[1] + y * M[5] + z * M[9] + M[13];
            o[2] = x * M[2] + y * M[6] + z * M[10] + M[14];
            o[3] = p[3]; o[4] = p[4];
            n[0] = nx * M[0] + ny * M[4] + nz * M[8];
            n[1] = nx * M[1] + ny * M[5] + nz * M[9];
            n[2] = nx * M[2] + ny * M[6] + nz * M[10];
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)m->nverts * 5 * sizeof(float), M ? pos : m->verts);
    glBindBuffer(GL_ARRAY_BUFFER, g->nbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)m->nverts * 3 * sizeof(float), nor);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    free(nor); free(pos);
}

/* ---- static-world batching ---- */

typedef struct { uint64_t key; int idx, cell_x, cell_y; unsigned char group; } BSortEnt;
/* Material provenance + reflective road: never mixed in one batch. The worker
   plan and its GL-thread check must agree on this exactly. */
/* A far LOD stand-in (world role 2) of a tall building: the skyline. Its
   batches are culled at the far plane instead of the shorter world view
   distance, so the city's towers do not pop in a cell at a time at 300 m. */
#define N2_BATCH_TALL_M 0.0f
static int batch_tall(const N2Mesh *m) {
    float tall_m = N2_BATCH_TALL_M;
#ifdef OPENUG2_SIM_PRE_H
    {   /* test aid (simulator): OPENUG2_TALL_M=<m> -- the stand-in height drawn to the far plane */
        static float env = -1.0f;
        if (env < 0.0f) env = getenv("OPENUG2_TALL_M") ? (float)atof(getenv("OPENUG2_TALL_M")) : N2_BATCH_TALL_M;
        tall_m = env;
    }
#endif
    return m->world_role == 2 && m->cat != N2_ROAD && m->cat != N2_TERRAIN && m->world_bounds_valid &&
           m->world_bounds[5] - m->world_bounds[2] >= tall_m;
}
static unsigned char batch_group(const N2Mesh *m, int mode) {
    return (unsigned char)(n2_world_batch_material_group(m, mode) | (m->reflect ? 0x80 : 0) |
                           (m->fade_in ? 0x40 : 0) |   /* never fade a road with its buildings */
                           (batch_tall(m) ? 0x20 : 0));
}
/* Settled far stand-ins (tall, not fading) batch on a coarser grid: they
   draw out to 500 m, where a 128 m cell per facade texture made them most of
   the extra draws of the pop-in pass (hwlog50: 146 stand-in draws/frame vs 30
   at the old 300 m cut). They are low-poly, so the bigger cells cost little
   to rebuild. A fading mesh keeps the tile cell: fades are per tile. */
#ifndef WORLD_STANDIN_CELL_M
#define WORLD_STANDIN_CELL_M 128.0f   /* 256 tried 11-14: no fewer draws once stand-ins reach by height (sim 313 vs 309), so off; sicell.txt for A/B */
#endif
#ifdef __vita__
static float standin_cell_m(void) {   /* ux0:data/NFSU2/sicell.txt "<m>" or OPENUG2_SICELL: A/B (128 = off) */
    static float v = -1.0f;
    if (v < 0.0f) {
        v = WORLD_STANDIN_CELL_M;
        const char *e = getenv("OPENUG2_SICELL");
        FILE *f = e ? NULL : fopen("ux0:data/NFSU2/sicell.txt", "r");
        if (e) v = (float)atof(e);
        else if (f) { float t; if (fscanf(f, "%f", &t) == 1) v = t; fclose(f); }
        if (v < 128.0f) v = 128.0f;
        printf("world: settled stand-ins batch on %.0f m cells\n", (double)v);
    }
    return v;
}
#endif
static int bsort_cmp(const void *a, const void *b) {
    const BSortEnt *aa = (const BSortEnt *)a, *bb = (const BSortEnt *)b;
    if (aa->cell_y != bb->cell_y) return aa->cell_y < bb->cell_y ? -1 : 1;
    if (aa->cell_x != bb->cell_x) return aa->cell_x < bb->cell_x ? -1 : 1;
    if (aa->key != bb->key) return aa->key < bb->key ? -1 : 1;
    if (aa->group != bb->group) return aa->group < bb->group ? -1 : 1;
    return aa->idx < bb->idx ? -1 : aa->idx > bb->idx ? 1 : 0;
}
static int btex_cmp(const void *a, const void *b) {   /* final texture order */
    GLuint ta = ((const N2Batch *)a)->tex, tb = ((const N2Batch *)b)->tex;
    return ta < tb ? -1 : ta > tb ? 1 : 0;
}

#define BATCH_CELL     256.0f   /* grid cell edge, metres */
#ifndef WORLD_BATCH_CELL_M
#define WORLD_BATCH_CELL_M 128.0f   /* Vita world batch cell edge (fixed grid), metres */
#endif
#define BATCH_MAXVERTS 65535    /* u16 indices (GLES2: no u32 without ext) */

#ifdef __vita__
/* GL-thread only. Cache owns no extra reference: the last resident release
 * removes the entry and frees its buffers. Unchanged tile/material runs share
 * GPU storage even while old and new CPU scenes coexist. */
typedef struct SharedBatchBuffers {
    struct SharedBatchBuffers *next;
    uint64_t hash[2];
    GLuint vbo, ibo;
    unsigned ioff;            /* indices' byte offset (ibo == vbo when one buffer) */
    int refs, nv, ni;
} SharedBatchBuffers;
#define SHARED_BATCH_SLOTS 4096
static SharedBatchBuffers *g_shared_batches[SHARED_BATCH_SLOTS];
/* The GL thread is the only writer; the world worker looks entries up
   (render_world_plan_fill), so insert/remove and that lookup take this. */
static volatile int g_sbl;
static void sbl_lock(void) { while (__sync_lock_test_and_set(&g_sbl, 1)) { } }
static void sbl_unlock(void) { __sync_lock_release(&g_sbl); }
static int g_batch_reuse;
static long g_batch_reused, g_batch_reused_bytes;

static void batch_payload_hash(const void *data, size_t bytes, uint64_t hash[2]) {
    const unsigned char *p = data;
    while (bytes >= 4) {
        uint32_t word; memcpy(&word, p, sizeof word);
        hash[0] = (hash[0] ^ word) * UINT64_C(1099511628211);
        hash[1] ^= (uint64_t)word + UINT64_C(0x9e3779b97f4a7c15) + (hash[1] << 6) + (hash[1] >> 2);
        p += 4; bytes -= 4;
    }
    for (size_t i = 0; i < bytes; i++) {
        hash[0] = (hash[0] ^ p[i]) * UINT64_C(1099511628211);
        hash[1] ^= (uint64_t)p[i] + UINT64_C(0x9e3779b97f4a7c15) + (hash[1] << 6) + (hash[1] >> 2);
    }
}

static void batch_fingerprint(const N2Scene *s, const BSortEnt *ent, int i0, int i1,
                              const PackedMesh *packed, const BatchedVertex *vertices,
                              uint64_t hash[2]) {
    hash[0]=UINT64_C(14695981039346656037); hash[1]=UINT64_C(0x6a09e667f3bcc909);
    int vo=0;
    for (int k=i0; k<i1; k++) {
        int idx=ent[k].idx; const N2Mesh *m=&s->meshes[idx];
        uint64_t piece[2]={UINT64_C(14695981039346656037),UINT64_C(0x6a09e667f3bcc909)};
        if (packed && packed[idx].hash_valid) memcpy(piece,packed[idx].hash,sizeof piece);
        else {
            batch_payload_hash(vertices+vo,(size_t)m->nverts*sizeof *vertices,piece);
            batch_payload_hash(m->idx,(size_t)m->nidx*sizeof *m->idx,piece);
        }
        /* Mesh boundaries and lengths define index offsets and payload order. */
        batch_payload_hash(&m->nverts,sizeof m->nverts,hash);
        batch_payload_hash(&m->nidx,sizeof m->nidx,hash);
        batch_payload_hash(piece,sizeof piece,hash);
        vo+=m->nverts;
    }
}

static SharedBatchBuffers *batch_buffers_find(const uint64_t hash[2], int nv, int ni) {
    for (SharedBatchBuffers *p = g_shared_batches[hash[0] & (SHARED_BATCH_SLOTS-1)]; p; p = p->next)
        if (p->hash[0] == hash[0] && p->hash[1] == hash[1] && p->nv == nv && p->ni == ni) return p;
    return NULL;
}
#endif

void render_batch_release(N2Batch *b) {
    if (!b) return;
#ifdef __vita__
    SharedBatchBuffers *p = b->shared_buffers;
    if (p) {
        assert(p->refs > 0);
        if (--p->refs == 0) {
            sbl_lock();
            SharedBatchBuffers **slot = &g_shared_batches[p->hash[0] & (SHARED_BATCH_SLOTS-1)];
            while (*slot && *slot != p) slot = &(*slot)->next;
            assert(*slot == p);
            *slot = p->next;
            sbl_unlock();
            GLuint ids[] = {p->vbo, p->ibo}; glDeleteBuffers(p->ibo == p->vbo ? 1 : 2, ids);   /* one buffer: once */
            free(p);
        }
    } else
#endif
    {
        GLuint ids[] = {b->vbo, b->ibo}; glDeleteBuffers(b->ibo == b->vbo ? 1 : 2, ids);
    }
    memset(b, 0, sizeof *b);
}

/* Milestone 75: the VBO layout draw_batch() hardcodes must equal the struct's
 * real layout. Compile-time (C99: negative array size, not C11 _Static_assert),
 * so a field reorder becomes a build error instead of scrambled attributes. */
typedef char n2_batchvertex_layout_check[
    (offsetof(BatchedVertex, pos)    ==  0 &&
     offsetof(BatchedVertex, uv)     == 12 &&
     offsetof(BatchedVertex, normal) == 20 &&
#ifdef __vita__
     offsetof(BatchedVertex, col)    == 24 &&
     sizeof(BatchedVertex)           == 28
#else
     offsetof(BatchedVertex, col)    == 32 &&
     sizeof(BatchedVertex)           == 36
#endif
    ) ? 1 : -1];

/* Milestone 79: dump one batch's member list, in the exact order
 * upload_world_batches partitioned it -- this runs inside batch_emit, so the
 * (ent, i0, i1) run IS the production run; nothing is re-sorted or re-derived. */
static void batch_members_report(const N2Scene *s, const BSortEnt *ent, int i0, int i1,
                                 int bidx, GLuint tex, const N2Batch *bref) {
    (void)bref;
    printf("MILESTONE: 79\n");
    printf("batch index   %d\n", bidx);
    printf("texkey        %08x   gl_tex %u   nmesh %d\n",
           s->meshes[ent[i0].idx].texkey, (unsigned)tex, i1 - i0);
    float mn[3] = {1e30f,1e30f,1e30f}, mx[3] = {-1e30f,-1e30f,-1e30f};
    for (int k = i0; k < i1; k++) {
        const N2Mesh *m = &s->meshes[ent[k].idx];
        for (int v = 0; v < m->nverts; v++)
            for (int c = 0; c < 3; c++) {
                float p = m->verts[v*5+c];
                if (p < mn[c]) mn[c] = p; if (p > mx[c]) mx[c] = p;
            }
    }
    printf("bounds        x[%.1f %.1f] y[%.1f %.1f] z[%.1f %.1f]\n",
           mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
    printf("%5s  %-30s %-9s %-44s %8s\n",
           "mesh", "asset name", "class", "bounds x/y/z", "tris");
    for (int k = i0; k < i1; k++) {
        const N2Mesh *m = &s->meshes[ent[k].idx];
        float a[3] = {1e30f,1e30f,1e30f}, b[3] = {-1e30f,-1e30f,-1e30f};
        for (int v = 0; v < m->nverts; v++)
            for (int c = 0; c < 3; c++) {
                float p = m->verts[v*5+c];
                if (p < a[c]) a[c] = p; if (p > b[c]) b[c] = p;
            }
        char bb[64];
        snprintf(bb, sizeof bb, "[%.0f %.0f][%.0f %.0f][%.1f %.1f]",
                 a[0], b[0], a[1], b[1], a[2], b[2]);
        printf("%5d  %-30s %-9s %-44s %8d\n",
               ent[k].idx, m->sname[0] ? m->sname : "(unnamed)",
               n2_scen_name(m->scen), bb, m->nidx / 3);
    }
}

/* Verify one source mesh survived the merge into `bv`/`bi` byte-exactly. */
static void batch_audit_report(const N2Scene *s, const BSortEnt *ent, int i0, int i1,
                               const BatchedVertex *bv, const uint16_t *bi,
                               int bidx, const char *want) {
    int vo = 0, io = 0;
    for (int k = i0; k < i1; k++) {
        const N2Mesh *m = &s->meshes[ent[k].idx];
        if (strcmp(m->sname, want)) { vo += m->nverts; io += m->nidx; continue; }

        int bad_pos = 0, bad_idx = 0, first_bad = -1;
        for (int v = 0; v < m->nverts; v++) {
            const float *p = m->verts + v*5;
            const BatchedVertex *o = &bv[vo + v];
            if (memcmp(o->pos, p, 3*sizeof(float))) {
                bad_pos++; if (first_bad < 0) first_bad = v;
            }
        }
        for (int t = 0; t < m->nidx; t++)
            if (bi[io + t] != (uint16_t)(m->idx[t] + vo)) bad_idx++;

        printf("MILESTONE: 75\n");
        printf("mesh name                        %s\n", m->sname);
        printf("source mesh index                %d\n", ent[k].idx);
        printf("source vertex count / index count %d / %d\n", m->nverts, m->nidx);
        printf("source first 3 position vertices ");
        for (int v = 0; v < 3 && v < m->nverts; v++)
            printf("(%.4f %.4f %.4f) ", m->verts[v*5], m->verts[v*5+1], m->verts[v*5+2]);
        printf("\nsource first 3 indices           ");
        for (int t = 0; t < 3 && t < m->nidx; t++) printf("%u ", m->idx[t]);
        printf("\nselected batch index             %d\n", bidx);
        printf("batch source-mesh count          %d\n", i1 - i0);
        printf("batched vertex offset            %d\n", vo);
        printf("batched first 3 corresponding position vertices ");
        for (int v = 0; v < 3 && v < m->nverts; v++)
            printf("(%.4f %.4f %.4f) ", bv[vo+v].pos[0], bv[vo+v].pos[1], bv[vo+v].pos[2]);
        printf("\nbatched remapped first 3 indices ");
        for (int t = 0; t < 3 && t < m->nidx; t++) printf("%u ", bi[io + t]);
        printf("\nvertex layout   pos@%d uv@%d normal@%d col@%d stride %d\n",
               (int)offsetof(BatchedVertex, pos), (int)offsetof(BatchedVertex, uv),
               (int)offsetof(BatchedVertex, normal), (int)offsetof(BatchedVertex, col),
               (int)sizeof(BatchedVertex));
        /* M98: UVs travel in the same vertex, so verify them the same way and
           report the ranges the sampler will actually see. Diagnostic only. */
        { int bad_uv = 0;
          float su0=1e30f, sv0=1e30f, su1=-1e30f, sv1=-1e30f;
          float bu0=1e30f, bv0=1e30f, bu1=-1e30f, bv1=-1e30f;
          float x0=1e30f, y0=1e30f, z0=1e30f, x1=-1e30f, y1=-1e30f, z1=-1e30f;
          for (int v = 0; v < m->nverts; v++) {
              const float *p = m->verts + v*5;
              const BatchedVertex *o = &bv[vo + v];
              if (memcmp(o->uv, p + 3, 2*sizeof(float))) bad_uv++;
              if (p[3]<su0)su0=p[3]; if (p[3]>su1)su1=p[3];
              if (p[4]<sv0)sv0=p[4]; if (p[4]>sv1)sv1=p[4];
              if (o->uv[0]<bu0)bu0=o->uv[0]; if (o->uv[0]>bu1)bu1=o->uv[0];
              if (o->uv[1]<bv0)bv0=o->uv[1]; if (o->uv[1]>bv1)bv1=o->uv[1];
              if (p[0]<x0)x0=p[0]; if (p[0]>x1)x1=p[0];
              if (p[1]<y0)y0=p[1]; if (p[1]>y1)y1=p[1];
              if (p[2]<z0)z0=p[2]; if (p[2]>z1)z1=p[2];
          }
          printf("texkey                           %08x\n", m->texkey);
          printf("source UV  min/max               u[%.4f %.4f] v[%.4f %.4f]  span %.3f x %.3f\n",
                 su0, su1, sv0, sv1, su1-su0, sv1-sv0);
          printf("batched UV min/max               u[%.4f %.4f] v[%.4f %.4f]\n",
                 bu0, bu1, bv0, bv1);
          printf("world span                       X %.3f Y %.3f Z %.3f  (m)\n",
                 x1-x0, y1-y0, z1-z0);
          printf("texels per metre (u,v vs X,Y)    %.4f / %.4f  [UV span / world span]\n",
                 (x1-x0) > 1e-6f ? (su1-su0)/(x1-x0) : 0.0f,
                 (y1-y0) > 1e-6f ? (sv1-sv0)/(y1-y0) : 0.0f);
          printf("UVs mismatched                   %d/%d\n", bad_uv, m->nverts);
        }
        printf("positions mismatched %d/%d   indices mismatched %d/%d",
               bad_pos, m->nverts, bad_idx, m->nidx);
        if (first_bad >= 0) printf("   (first bad vertex %d)", first_bad);
        printf("\nmax remapped index %d vs batch vertex ceiling %d\n",
               (int)(m->idx[0] + vo), BATCH_MAXVERTS);
        printf("RESULT: %s\n", (bad_pos || bad_idx) ? "MISMATCH" : "MATCH");
        return;
    }
}

/* A batch's CPU copy lives only until glBufferData copies it: one grow-only
   pair (vertices, indices) on the GL thread instead of a malloc + free per
   batch, which from 64 KB up is a kernel block each (sdl_vita.c BIGBLK_MIN)
   and used to leave heap holes between streaming builds. */
static void *g_batch_scr[2]; static size_t g_batch_scr_cap[2];
static void *batch_scratch(int k, size_t n) {
    if (n > g_batch_scr_cap[k]) {
        size_t cap = (n + (n >> 2) + 0xFFFF) & ~(size_t)0xFFFF;
        free(g_batch_scr[k]);
        g_batch_scr[k] = malloc(cap);
        g_batch_scr_cap[k] = g_batch_scr[k] ? cap : 0;
    }
    return g_batch_scr[k];
}

/* merge meshes [i0,i1) of the sort array into one uploaded batch */
static float *const *g_batch_normals;   /* set for the duration of a step */
static PackedMesh *g_batch_packed;
static const uint64_t *g_batch_prepared_hash;
#ifdef __vita__
unsigned long long sceKernelGetProcessTimeWide(void);
static unsigned long long g_batch_pack_us, g_batch_gl_us, g_batch_alloc_us, g_batch_push_us;
static long g_batch_src_meshes, g_batch_src_verts;   /* hash-only meshes rebuilt from source */
static long g_batch_asm, g_batch_deferred;   /* batches uploaded from worker-assembled arrays */
static long g_batch_bytes;
/* worker-assembled arrays copied on the GPU-copy worker (sdl_vita.c);
   ux0:data/NFSU2/nogpucopy.txt copies inline as before */
static int g_batch_copy, g_batch_onebuf;   /* twobuf.txt: separate vertex/index buffers as before */
static long g_batch_copied;
static unsigned long long g_batch_wait_us;
#endif

/* A batch of the worker's plan. meta: bb/scen/named computed on the worker
   (11-14 night); bv/bi: the batch's final vertex and index arrays, assembled
   on the worker when the GPU did not hold it at plan time (in the plan's
   arena), so the GL thread only uploads them. */
struct PreparedBatch {
    int begin, end; uint64_t hash[2];
    int meta, named, scen[8], nv, ni; float bb[6];
    BatchedVertex *bv; uint16_t *bi;
};
static const struct PreparedBatch *g_batch_prep;   /* set for the duration of an emit */

static int batch_emit(const N2Scene *s, const BSortEnt *ent, int i0, int i1,
                       GLuint tex, N2Batch *b, int bidx, const char *audit,
                       const unsigned char *mtexmode) {
    int nv = 0, ni = 0;
    for (int k = i0; k < i1; k++) {
        nv += s->meshes[ent[k].idx].nverts; ni += s->meshes[ent[k].idx].nidx;
    }
#ifdef __vita__
    unsigned long long g_batch_t0 = sceKernelGetProcessTimeWide();
    const struct PreparedBatch *pb = g_batch_prep;
    if (g_batch_reuse && pb && pb->meta && !audit) {
        /* the worker did the per-mesh work: reuse or upload, no mesh walk */
        const N2Mesh *m0 = &s->meshes[ent[i0].idx];
        SharedBatchBuffers *p = batch_buffers_find(pb->hash, nv, ni);
        int up = 0;
        memset(b, 0, sizeof *b);
        if (p) {
            p->refs++; b->vbo = p->vbo; b->ibo = p->ibo; b->ioff = p->ioff; b->shared_buffers = p;
            g_batch_reused++; g_batch_reused_bytes += (long)nv*sizeof(BatchedVertex)+(long)ni*2;
        } else if (pb->bv) {
            static int verify = -1;
            if (verify < 0) { const char *e = getenv("OPENUG2_ASM_VERIFY"); verify = e && e[0] == '1'; }
            if (verify) {   /* sim check: the arrays equal a rebuild from the source meshes */
                static long vok, vbad;
                int vo = 0, io = 0, bad = 0;
                for (int k = i0; k < i1 && !bad; k++) {
                    const N2Mesh *m = &s->meshes[ent[k].idx];
                    float *nor = (float *)calloc((size_t)m->nverts * 3, sizeof(float));
                    if (!nor) break;
                    mesh_normals(m, nor);
                    for (int v = 0; v < m->nverts && !bad; v++) {
                        BatchedVertex o; memset(&o, 0, sizeof o); const float *q = m->verts + v*5;
                        o.pos[0]=q[0]; o.pos[1]=q[1]; o.pos[2]=q[2]; o.uv[0]=q[3]; o.uv[1]=q[4];
                        BV_SET_NORMAL(&o, nor[v*3], nor[v*3+1], nor[v*3+2]);
                        if (m->vcol) { o.col[0]=m->vcol[v*4]; o.col[1]=m->vcol[v*4+1];
                                       o.col[2]=m->vcol[v*4+2]; o.col[3]=m->vcol[v*4+3]; }
                        else o.col[0]=o.col[1]=o.col[2]=o.col[3]=255;
                        const BatchedVertex *a = &pb->bv[vo + v];
                        if (memcmp(a->pos, o.pos, sizeof o.pos) || memcmp(a->uv, o.uv, sizeof o.uv) ||
                            memcmp(a->col, o.col, sizeof o.col)) bad = 1;
                    }
                    for (int t = 0; t < m->nidx && !bad; t++)
                        if (pb->bi[io + t] != (uint16_t)(m->idx[t] + vo)) bad = 2;
                    free(nor); vo += m->nverts; io += m->nidx;
                }
                if (bad) vbad++; else vok++;
                if (bad || ((vok + vbad) & 255) == 0)
                    fprintf(stderr, "asm verify: %ld ok, %ld bad (last batch %d: %d)\n", vok, vbad, bidx, bad);
            }
            unsigned long long tg = sceKernelGetProcessTimeWide();
            g_batch_pack_us += tg - g_batch_t0;
            /* allocate here, copy on the GPU-copy worker (11-15, PERF_PLAN
               P2.3); the step finishes only once the copies are done, and
               the arrays live in the plan's arena until then */
            void *dv = NULL, *di = NULL;
            if (g_batch_copy && g_batch_onebuf) {
                /* one buffer a batch, indices after the vertices (11-15
                   hwlog66: two allocations a batch were most of the
                   60-133 ms of GL buffers at a big handoff) */
                size_t vb = (size_t)nv * sizeof(BatchedVertex);
                glGenBuffers(1, &b->vbo); glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
                glBufferData(GL_ARRAY_BUFFER, (long)(vb + (size_t)ni * 2), NULL, GL_STATIC_DRAW);
                dv = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
                b->ibo = b->vbo; b->ioff = (unsigned)vb;
                di = dv ? (unsigned char *)dv + vb : NULL;
                if (glGetError() != GL_NO_ERROR || !dv) {
                    glDeleteBuffers(1, &b->vbo);
                    memset(b, 0, sizeof *b); return 0;
                }
            } else {
            glGenBuffers(1, &b->vbo); glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
            glBufferData(GL_ARRAY_BUFFER, (long)nv * (long)sizeof(BatchedVertex), g_batch_copy ? NULL : pb->bv, GL_STATIC_DRAW);
            if (g_batch_copy) dv = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
            glGenBuffers(1, &b->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)ni * 2, g_batch_copy ? NULL : pb->bi, GL_STATIC_DRAW);
            if (g_batch_copy) di = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
            if (glGetError() != GL_NO_ERROR || (g_batch_copy && (!dv || !di))) {
                glDeleteBuffers(1, &b->vbo); glDeleteBuffers(1, &b->ibo);
                memset(b, 0, sizeof *b); return 0;
            }
            }
            if (g_batch_copy) {
                unsigned long long tp = sceKernelGetProcessTimeWide();
                g_batch_alloc_us += tp - tg;
                vita_copy_push(dv, pb->bv, (unsigned)nv * (unsigned)sizeof(BatchedVertex));
                vita_copy_push(di, pb->bi, (unsigned)ni * 2u);
                /* vitaGL keeps the pointer (unmap only clears its flag); the
                   simulator's desktop GL copied inline and needs it unmapped */
                if (b->ibo != b->vbo) glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
                glUnmapBuffer(GL_ARRAY_BUFFER);
                g_batch_push_us += sceKernelGetProcessTimeWide() - tp;
                g_batch_copied++;
            }
            g_batch_bytes += (long)nv * (long)sizeof(BatchedVertex) + (long)ni * 2;
            SharedBatchBuffers *q = calloc(1, sizeof *q);
            if (q) {
                q->hash[0] = pb->hash[0]; q->hash[1] = pb->hash[1];
                q->nv = nv; q->ni = ni; q->refs = 1; q->vbo = b->vbo; q->ibo = b->ibo; q->ioff = b->ioff;
                unsigned slot = pb->hash[0] & (SHARED_BATCH_SLOTS-1);
                sbl_lock(); q->next = g_shared_batches[slot]; g_shared_batches[slot] = q; sbl_unlock();
                b->shared_buffers = q;
            }
            g_batch_gl_us += sceKernelGetProcessTimeWide() - tg;
            g_batch_asm++;
            up = 1;
        }
        if (p || up) {
            b->index_count = ni; b->tex = tex; b->nmesh = i1 - i0; b->emit_idx = bidx;
            b->texkey = m0->texkey;
            b->drawmode = mtexmode ? mtexmode[ent[i0].idx] : N2_DRAW_OPAQUE;
            b->reflect = m0->reflect; b->fade_in = m0->fade_in;
            b->tall = (unsigned char)batch_tall(m0);
            memcpy(b->scen_count, pb->scen, sizeof b->scen_count);
            b->unresolved = (unsigned char)(!tex && pb->named == i1 - i0);
            for (int c = 0; c < 3; c++) { b->bbox_min[c] = pb->bb[c]; b->bbox_max[c] = pb->bb[c+3]; }
            if (g_batch_packed) for (int k = i0; k < i1; k++) {   /* own copies left (none, usually) */
                PackedMesh *pk = &g_batch_packed[ent[k].idx];
                if (pk->v && !pk->borrowed) free(pk->v);
                pk->v = NULL;
            }
            if (p) g_batch_pack_us += sceKernelGetProcessTimeWide() - g_batch_t0;
            return 1;
        }
        /* neither: fall through to the mesh path (pk->v or the source) */
    }
    if (g_batch_reuse && g_batch_prepared_hash && g_batch_packed && !audit) {
        SharedBatchBuffers *p=batch_buffers_find(g_batch_prepared_hash,nv,ni);
        if (p) {
            memset(b,0,sizeof *b); p->refs++;
            b->vbo=p->vbo; b->ibo=p->ibo; b->ioff=p->ioff; b->shared_buffers=p;
            b->index_count=ni; b->tex=tex; b->nmesh=i1-i0; b->emit_idx=bidx;
            b->texkey=s->meshes[ent[i0].idx].texkey;
            b->drawmode=mtexmode ? mtexmode[ent[i0].idx] : N2_DRAW_OPAQUE;
            b->reflect=s->meshes[ent[i0].idx].reflect;   /* as the upload path below */
            b->fade_in=s->meshes[ent[i0].idx].fade_in;
            b->tall=batch_tall(&s->meshes[ent[i0].idx]);
            for (int c=0; c<3; c++) { b->bbox_min[c]=1e30f; b->bbox_max[c]=-1e30f; }
            int named=0;
            for (int k=i0; k<i1; k++) {
                int idx=ent[k].idx; const N2Mesh *m=&s->meshes[idx];
                PackedMesh *pk=&g_batch_packed[idx]; assert(pk->hash_valid);
                for (int c=0; c<3; c++) {
                    if (pk->bb[c]<b->bbox_min[c]) b->bbox_min[c]=pk->bb[c];
                    if (pk->bb[c+3]>b->bbox_max[c]) b->bbox_max[c]=pk->bb[c+3];
                }
                if (m->scen<8) b->scen_count[m->scen]++;
                if (m->texkey) named++;
                if (!pk->borrowed) free(pk->v);
                pk->v=NULL;
            }
            b->unresolved=(unsigned char)(!tex && named==i1-i0);
            g_batch_reused++; g_batch_reused_bytes+=(long)nv*sizeof(BatchedVertex)+(long)ni*2;
            g_batch_pack_us+=sceKernelGetProcessTimeWide()-g_batch_t0;
            return 1;
        }
    }
#endif
    BatchedVertex *bv = (BatchedVertex *)batch_scratch(0, (size_t)nv * sizeof *bv);
    uint16_t *bi = (uint16_t *)batch_scratch(1, (size_t)ni * sizeof *bi);
    if (!bv || !bi) return 0;
    float mn[3] = {1e30f,1e30f,1e30f}, mx[3] = {-1e30f,-1e30f,-1e30f};
    int vo = 0, io = 0;
    for (int k = i0; k < i1; k++) {
        const N2Mesh *m = &s->meshes[ent[k].idx];
        PackedMesh *pk = g_batch_packed ? &g_batch_packed[ent[k].idx] : NULL;
        if (pk && pk->v) {                      /* prepared by the worker */
            memcpy(bv + vo, pk->v, (size_t)m->nverts * sizeof *bv);
            for (int c = 0; c < 3; c++) {
                if (pk->bb[c] < mn[c]) mn[c] = pk->bb[c];
                if (pk->bb[c+3] > mx[c]) mx[c] = pk->bb[c+3];
            }
            for (int t = 0; t < m->nidx; t++) bi[io + t] = (uint16_t)(m->idx[t] + vo);
            /* Each ordinary source mesh belongs to exactly one emission run.
               Release staging as it is consumed, spreading frees across steps
               and lowering heap pressure while GPU residents coexist. */
            if (!pk->borrowed) free(pk->v);
            pk->v = NULL;
            vo += m->nverts; io += m->nidx;
            continue;
        }
#ifdef __vita__
        if (pk && pk->hash_valid) { g_batch_src_meshes++; g_batch_src_verts += m->nverts; }
#endif
        float *pre = g_batch_normals ? g_batch_normals[ent[k].idx] : NULL;
        float *nor = pre ? pre : (float *)calloc((size_t)m->nverts * 3, sizeof(float));
        if (!nor) return 0;
        if (!pre) mesh_normals(m, nor);   /* per source mesh: no cross-mesh smoothing */
        for (int v = 0; v < m->nverts; v++) {
            BatchedVertex *o = &bv[vo + v]; const float *p = m->verts + v*5;
            o->pos[0]=p[0]; o->pos[1]=p[1]; o->pos[2]=p[2];
            o->uv[0]=p[3];  o->uv[1]=p[4];
            BV_SET_NORMAL(o, nor[v*3], nor[v*3+1], nor[v*3+2]);
            if (m->vcol) { o->col[0]=m->vcol[v*4]; o->col[1]=m->vcol[v*4+1];
                           o->col[2]=m->vcol[v*4+2]; o->col[3]=m->vcol[v*4+3]; }
            else { o->col[0]=o->col[1]=o->col[2]=o->col[3]=255; }  /* neutral */
            for (int c = 0; c < 3; c++) {
                if (p[c] < mn[c]) mn[c] = p[c];
                if (p[c] > mx[c]) mx[c] = p[c];
            }
        }
        for (int t = 0; t < m->nidx; t++) bi[io + t] = (uint16_t)(m->idx[t] + vo);
        vo += m->nverts; io += m->nidx;
        if (!pre) free(nor);
    }
    if (audit && audit[0] != '#') batch_audit_report(s, ent, i0, i1, bv, bi, bidx, audit);
#ifdef __vita__
    unsigned long long t_gl = sceKernelGetProcessTimeWide();
    g_batch_pack_us += t_gl - g_batch_t0;
#endif
    memset(b, 0, sizeof *b);
    int reused = 0;
#ifdef __vita__
    uint64_t payload_hash[2] = {UINT64_C(14695981039346656037), UINT64_C(0x6a09e667f3bcc909)};
    if (g_batch_reuse) {
        if (g_batch_prepared_hash) memcpy(payload_hash,g_batch_prepared_hash,sizeof payload_hash);
        else batch_fingerprint(s,ent,i0,i1,g_batch_packed,bv,payload_hash);
        SharedBatchBuffers *p = batch_buffers_find(payload_hash, nv, ni);
        if (p) {
            p->refs++;
            b->vbo = p->vbo; b->ibo = p->ibo; b->ioff = p->ioff; b->shared_buffers = p;
            reused = 1; g_batch_reused++;
            g_batch_reused_bytes += (long)nv * sizeof *bv + (long)ni * sizeof *bi;
        }
    }
#endif
    if (!reused) {
#ifdef __vita__
    unsigned long long after_lookup = sceKernelGetProcessTimeWide();
    g_batch_pack_us += after_lookup - t_gl; t_gl = after_lookup;
#endif
    glGenBuffers(1, &b->vbo); glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)nv * (long)sizeof *bv, bv, GL_STATIC_DRAW);
#ifdef __vita__
    g_batch_bytes += (long)nv * (long)sizeof *bv + (long)ni * 2;
#endif
    glGenBuffers(1, &b->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)ni * 2, bi, GL_STATIC_DRAW);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteBuffers(1, &b->vbo); glDeleteBuffers(1, &b->ibo);
        memset(b, 0, sizeof *b); return 0;
    }
#ifdef __vita__
    if (g_batch_reuse) {
        SharedBatchBuffers *p = calloc(1, sizeof *p);
        if (p) {
            p->hash[0] = payload_hash[0]; p->hash[1] = payload_hash[1];
            p->nv = nv; p->ni = ni; p->refs = 1; p->vbo = b->vbo; p->ibo = b->ibo;
            unsigned slot = payload_hash[0] & (SHARED_BATCH_SLOTS-1);
            sbl_lock();
            p->next = g_shared_batches[slot]; g_shared_batches[slot] = p;
            sbl_unlock();
            b->shared_buffers = p;
        }
    }
#endif
    }
    b->index_count = ni; b->tex = tex; b->nmesh = i1 - i0; b->emit_idx = bidx;
    b->texkey = s->meshes[ent[i0].idx].texkey;
    b->drawmode = mtexmode ? mtexmode[ent[i0].idx] : N2_DRAW_OPAQUE;
    b->reflect = s->meshes[ent[i0].idx].reflect;
    b->fade_in = s->meshes[ent[i0].idx].fade_in;
    b->tall = (unsigned char)batch_tall(&s->meshes[ent[i0].idx]);
    int named = 0;
    for (int k = i0; k < i1; k++) {
        int sc = s->meshes[ent[k].idx].scen;
        if (sc >= 0 && sc < 8) b->scen_count[sc]++;
        if (s->meshes[ent[k].idx].texkey) named++;
    }
    /* Only a batch with no GL texture can be "missing art", and only when every
       member asked for one. Measured over five shipped bundles: 68 batches are
       wholly unresolved and NOT ONE mixes the two kinds, so this changes no
       shipped frame -- it removes the dependence on which member happened to
       sort first, which is what `texkey` reported. */
    b->unresolved = (unsigned char)(!tex && named == i1 - i0);
    for (int c = 0; c < 3; c++) { b->bbox_min[c] = mn[c]; b->bbox_max[c] = mx[c]; }
#ifdef __vita__
    g_batch_gl_us += sceKernelGetProcessTimeWide() - t_gl;
#endif
    return 1;
}

/* plan_assemble's slabs (11-14 P0.1): small blocks fit the heap's holes */
#define PLAN_SLAB_BYTES (256u << 10)
typedef struct PlanSlab { struct PlanSlab *next; size_t cap, used, pad; } PlanSlab;   /* 16-byte header */
static long g_plan_slab_fail;
static void *plan_slab_alloc(size_t n) { return malloc(n); }
static void plan_slabs_free(void *head) {
    for (PlanSlab *p = (PlanSlab *)head; p;) { PlanSlab *n = p->next; free(p); p = n; }
}
struct WorldBatchUpload {
    PackedMesh *packed;
    float *const *normals;
    const N2Scene *scene;
    const unsigned char *modes;
    const char *audit;
    BSortEnt *ent;
    N2Batch *batches;
    int *run0, *run1;
    int meshes, next, count;
    int tail;   /* smooth finish: stats + texture sort done, mesh map + frees next call */
    struct PreparedBatch *prepared;
    int prepared_count;
    void *arena;   /* the prepared batches' assembled arrays (render_world_plan_fill): a chain of PlanSlab */
};

void upload_world_batches_cancel(WorldBatchUpload **slot) {
    if (!slot || !*slot) return;
    WorldBatchUpload *j = *slot;
#ifdef __vita__
    vita_copy_wait();   /* copies into this job's buffers from its arena */
#endif
    render_batch_array_free(&j->batches, &j->count);
    plan_slabs_free(j->arena); free(j->prepared); free(j->ent); free(j->run0); free(j->run1); free(j);
    *slot = NULL;
}

WorldBatchUpload *upload_world_batches_begin(const N2Scene *s,
                          const float (*mbb)[4], const GLuint *mtex,
                          GLuint texTerr, const char *audit,
                          const unsigned char *mtexmode) {
    if (!s || s->count < 0 || (s->count && (!s->meshes || !mbb || !mtex)))
        return NULL;
    WorldBatchUpload *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    int n = s->count;
    size_t cap = (size_t)(n ? n : 1);
    /* ponytail: at most one batch per source mesh; temporary upper-bound
     * arrays avoid reallocating partial GL owners. Compact if memory dominates. */
    j->ent = malloc(cap * sizeof *j->ent);
    j->batches = malloc(cap * sizeof *j->batches);
    j->run0 = malloc(cap * sizeof *j->run0);
    j->run1 = malloc(cap * sizeof *j->run1);
    if (!j->ent || !j->batches || !j->run0 || !j->run1) {
        upload_world_batches_cancel(&j); return NULL;
    }
    j->scene = s; j->modes = mtexmode; j->audit = audit;
    BSortEnt *ent = j->ent;
    /* world extent -> grid coords */
    float x0 = 1e30f, y0 = 1e30f;
    for (int i = 0; i < n; i++) {
        if (mbb[i][0] < x0) x0 = mbb[i][0];
        if (mbb[i][1] < y0) y0 = mbb[i][1];
    }
    /* sort meshes by (cell, resolved texture) — sky/glow meshes are pulled by
       upload_cat_batches instead: batching them in here would let an ordinary
       cell+texture run silently absorb a skybox shell or a neon sign, so
       they'd draw with the wrong depth/blend state at the wrong time. */
    int m = 0;
    for (int i = 0; i < n; i++) {
        const N2Mesh *mesh = &s->meshes[i];
        /* role 3: a smokeable prop, drawn by smokeable.c */
        if (mesh->world_role == 1 || mesh->world_role == 3 || mesh->cat == N2_SKY || mesh->cat == N2_GLOW) continue;
        GLuint tex = mtex[i];
        if (!tex && mesh->cat == N2_TERRAIN) tex = texTerr;   /* fallback baked in */
        float cx = (mbb[i][0]+mbb[i][2])*0.5f, cy = (mbb[i][1]+mbb[i][3])*0.5f;
        uint64_t cell = (uint64_t)(uint32_t)((int)((cy-y0)/BATCH_CELL)*4096
                                           + (int)((cx-x0)/BATCH_CELL));
        ent[m].key = cell << 32 | tex; ent[m].idx = i;
        ent[m].cell_x = ent[m].cell_y = 0;
#ifdef __vita__
        /* Fixed world tiles; resident bounds must not shift batch membership. */
        ent[m].cell_x = (int)floorf(cx / WORLD_BATCH_CELL_M);
        ent[m].cell_y = (int)floorf(cy / WORLD_BATCH_CELL_M);
        ent[m].key = tex;
#endif
        ent[m].group = batch_group(mesh, mtexmode ? mtexmode[i] : N2_DRAW_OPAQUE);
#ifdef __vita__
        if ((ent[m].group & 0x20) && !mesh->fade_in && standin_cell_m() > WORLD_BATCH_CELL_M) {
            ent[m].cell_x = (int)floorf(cx / standin_cell_m());
            ent[m].cell_y = (int)floorf(cy / standin_cell_m());
        }
#endif
        m++;
    }
    qsort(ent, (size_t)m, sizeof *ent, bsort_cmp);
    j->meshes = m;
#ifdef __vita__
    {   static int cellstat = -1;   /* test aid: OPENUG2_CELLSTAT prints the batch count at 128/256/512 m cells */
        if (cellstat < 0) cellstat = getenv("OPENUG2_CELLSTAT") != NULL;
        if (cellstat && m) {
            int cnt[3] = {0};
            for (int sh = 0; sh < 3; sh++) {
                typedef struct { int x, y, g; GLuint t; } K;
                K *kk = (K *)malloc((size_t)m * sizeof *kk); if (!kk) break;
                for (int i = 0; i < m; i++) { kk[i].x = ent[i].cell_x >> sh; kk[i].y = ent[i].cell_y >> sh;
                    kk[i].g = ent[i].group; kk[i].t = (GLuint)ent[i].key; }
                for (int i = 0; i < m; i++) { int dup = 0;
                    for (int q = i - 1; q >= 0 && q >= i - 4000 && !dup; q--)
                        dup = kk[q].x == kk[i].x && kk[q].y == kk[i].y && kk[q].g == kk[i].g && kk[q].t == kk[i].t;
                    cnt[sh] += !dup; }
                free(kk);
            }
            printf("cellstat: %d meshes -> batches 128m %d, 256m %d, 512m %d (ignoring the vertex cap)\n", m, cnt[0], cnt[1], cnt[2]);
        }
    }
#endif
    return j;
}

void upload_world_batches_set_normals(WorldBatchUpload *job, float *const *normals) {
    if (job) job->normals = normals;
}
void render_mesh_normals(const N2Mesh *m, float *nor) { mesh_normals(m, nor); }

void upload_world_batches_set_packed(WorldBatchUpload *job, PackedMesh *packed) {
    if (job) job->packed = packed;
}

WorldBatchUpload *upload_world_batches_prepare_cpu(const N2Scene *s,
    const float (*bounds)[4], const GLuint *logical_textures,
    const unsigned char *modes, PackedMesh *packed) {
#ifdef __vita__
    /* Metadata only: avoid a second resident-sized packed allocation. */
    WorldBatchUpload *j=upload_world_batches_begin(s,bounds,logical_textures,0,NULL,modes);
    if (!j) return NULL;
    j->modes=NULL;
    j->prepared=calloc((size_t)(j->meshes ? j->meshes : 1),sizeof *j->prepared);
    if (!j->prepared) { upload_world_batches_cancel(&j); return NULL; }
    for (int i0=0; i0<j->meshes;) {
        extern int world_build_aborted(void);
        if (world_build_aborted()) { upload_world_batches_cancel(&j); return NULL; }
        int i=i0+1, verts=s->meshes[j->ent[i0].idx].nverts;
        while (i<j->meshes && j->ent[i].key==j->ent[i0].key &&
               j->ent[i].cell_x==j->ent[i0].cell_x && j->ent[i].cell_y==j->ent[i0].cell_y &&
               j->ent[i].group==j->ent[i0].group &&
               verts+s->meshes[j->ent[i].idx].nverts<=BATCH_MAXVERTS)
            verts+=s->meshes[j->ent[i++].idx].nverts;
        for (int k=i0; k<i; k++) if (!packed || !packed[j->ent[k].idx].hash_valid) {
            upload_world_batches_cancel(&j); return NULL;
        }
        struct PreparedBatch *p=&j->prepared[j->prepared_count++]; p->begin=i0; p->end=i;
        batch_fingerprint(s,j->ent,i0,i,packed,NULL,p->hash);
        i0=i;
    }
    j->packed=packed;
    return j;
#else
    (void)s; (void)bounds; (void)logical_textures; (void)modes; (void)packed;
    return NULL;
#endif
}

/* Worker, after upload_world_batches_prepare_cpu: the planned batches the GPU
   does not hold yet get their meshes packed here, so the GL thread only
   copies them (11-14: with the CPU copies freed after upload, rebuilding those
   meshes there took packing from 27 to 37 ms a build and the stream frames
   missed more often, hwlog51_I). Returns the meshes packed. */
#ifdef __vita__
/* Worker, end of render_world_plan_fill: each prepared batch's bounds, scene
   counts and named-texture count, and -- for the batches the GPU lacks, while
   an 8 MB arena lasts -- its final arrays (vertices concatenated, indices
   rebased), so batch_emit on the GL thread does no per-mesh work (hwlog51_L:
   26 ms of CPU packing per build there). Own packed copies are freed here
   once assembled. */
/* 11-15 hwlog65: 8 MB covered 1014 of 2490 new batches on a 20 MB handoff;
   the rest packed + copied on the frame thread (86 + 143 ms). The arrays
   replace the packed copies they are built from, so 16 MB costs little more (big blocks peak 122 of 125 MB: not more). */
#define PLAN_ARENA_MAX (16u << 20)
static void plan_assemble(WorldBatchUpload *j) {
    /* 11-14 hwlog55: one 4-8 MB malloc here failed downtown with 35-45 MB
       free (the heap in small holes). The arrays now go in 256 KB slabs (a
       batch bigger than that gets a slab of its own size); a slab that
       cannot be had leaves the rest of the batches to batch_emit's own path. */
    extern int world_build_aborted(void);
    if (!j || !j->prepared || !j->packed) return;
    unsigned char *want = (unsigned char *)calloc((size_t)(j->prepared_count ? j->prepared_count : 1), 1);
    if (!want) return;
    (void)0;
    size_t total = 0;
    for (int b = 0; b < j->prepared_count; b++) {
        struct PreparedBatch *p = &j->prepared[b];
        int nv = 0, ni = 0, ok = 1;
        for (int c = 0; c < 3; c++) { p->bb[c] = 1e30f; p->bb[c+3] = -1e30f; }
        memset(p->scen, 0, sizeof p->scen); p->named = 0;
        for (int k = p->begin; k < p->end; k++) {
            int idx = j->ent[k].idx; const N2Mesh *m = &j->scene->meshes[idx];
            const PackedMesh *pk = &j->packed[idx];
            nv += m->nverts; ni += m->nidx;
            for (int c = 0; c < 3; c++) {
                if (pk->bb[c] < p->bb[c]) p->bb[c] = pk->bb[c];
                if (pk->bb[c+3] > p->bb[c+3]) p->bb[c+3] = pk->bb[c+3];
            }
            if (m->scen >= 0 && m->scen < 8) p->scen[m->scen]++;
            if (m->texkey) p->named++;
            ok &= pk->v != NULL;
        }
        p->meta = 1; p->nv = nv; p->ni = ni;
        if (!ok) continue;
        sbl_lock(); int have = batch_buffers_find(p->hash, nv, ni) != NULL; sbl_unlock();
        if (have) continue;
        size_t need = ((size_t)nv * sizeof(BatchedVertex) + (size_t)ni * 2 + 15) & ~(size_t)15;
        if (total + need > PLAN_ARENA_MAX) continue;
        want[b] = 1; total += need;
    }
    {
        PlanSlab *cur = NULL;
        int slab_failed = 0;
        for (int b = 0; b < j->prepared_count && !slab_failed; b++) {
            if (!want[b]) continue;
            struct PreparedBatch *p = &j->prepared[b];
            int nv = 0, ni = 0;
            for (int k = p->begin; k < p->end; k++) {
                const N2Mesh *m = &j->scene->meshes[j->ent[k].idx]; nv += m->nverts; ni += m->nidx;
            }
            size_t need = ((size_t)nv * sizeof(BatchedVertex) + (size_t)ni * 2 + 15) & ~(size_t)15;
            if (!cur || cur->cap - cur->used < need) {
                size_t cap = need > PLAN_SLAB_BYTES ? need : PLAN_SLAB_BYTES;
                PlanSlab *ns = (PlanSlab *)plan_slab_alloc(sizeof(PlanSlab) + cap);
                if (!ns) { slab_failed = 1; g_plan_slab_fail++; printf("world batches: slab of %u KB refused (%ld so far); rest via batch_emit\n", (unsigned)(cap >> 10), g_plan_slab_fail); break; }
                ns->cap = cap; ns->used = 0; ns->next = (PlanSlab *)j->arena; j->arena = ns;
                cur = ns;
            }
            unsigned char *at = (unsigned char *)(cur + 1) + cur->used;
            cur->used += need;
            BatchedVertex *bv = (BatchedVertex *)at;
            uint16_t *bi = (uint16_t *)(at + (size_t)nv * sizeof *bv);
            int vo = 0, io = 0;
            for (int k = p->begin; k < p->end; k++) {
                const N2Mesh *m = &j->scene->meshes[j->ent[k].idx];
                PackedMesh *pk = &j->packed[j->ent[k].idx];
                memcpy(bv + vo, pk->v, (size_t)m->nverts * sizeof *bv);
                for (int t = 0; t < m->nidx; t++) bi[io + t] = (uint16_t)(m->idx[t] + vo);
                vo += m->nverts; io += m->nidx;
            }
            p->bv = bv; p->bi = bi;
            for (int k = p->begin; k < p->end; k++) {   /* own copies: done with */
                PackedMesh *pk = &j->packed[j->ent[k].idx];
                if (!pk->borrowed) { free(pk->v); pk->v = NULL; }
            }
        }
    }
    free(want);
}
#endif
int render_world_plan_fill(WorldBatchUpload *j) {
#ifdef __vita__
    if (!j || !j->prepared || !j->packed) return 0;
    extern int world_build_aborted(void);
    int filled = 0;
    for (int b = 0; b < j->prepared_count; b++) {
        if ((b & 31) == 0 && world_build_aborted()) break;
        struct PreparedBatch *p = &j->prepared[b];
        int nv = 0, ni = 0, need = 0;
        for (int k = p->begin; k < p->end; k++) {
            const N2Mesh *m = &j->scene->meshes[j->ent[k].idx];
            nv += m->nverts; ni += m->nidx;
            need |= !j->packed[j->ent[k].idx].v;
        }
        if (!need) continue;
        sbl_lock(); int have = batch_buffers_find(p->hash, nv, ni) != NULL; sbl_unlock();
        if (have) continue;
        for (int k = p->begin; k < p->end; k++) {
            PackedMesh *pk = &j->packed[j->ent[k].idx];
            if (pk->v || !pk->hash_valid) continue;
            const N2Mesh *m = &j->scene->meshes[j->ent[k].idx];
            BatchedVertex *bv = (BatchedVertex *)malloc((size_t)m->nverts * sizeof *bv);
            float *nor = (float *)calloc((size_t)m->nverts * 3, sizeof(float));
            if (!bv || !nor) { free(bv); free(nor); continue; }
            mesh_normals(m, nor);
            for (int v = 0; v < m->nverts; v++) {   /* as render_pack_mesh */
                BatchedVertex *o = &bv[v]; const float *q = m->verts + v*5;
                o->pos[0]=q[0]; o->pos[1]=q[1]; o->pos[2]=q[2];
                o->uv[0]=q[3];  o->uv[1]=q[4];
                BV_SET_NORMAL(o, nor[v*3], nor[v*3+1], nor[v*3+2]);
                if (m->vcol) { o->col[0]=m->vcol[v*4]; o->col[1]=m->vcol[v*4+1];
                               o->col[2]=m->vcol[v*4+2]; o->col[3]=m->vcol[v*4+3]; }
                else { o->col[0]=o->col[1]=o->col[2]=o->col[3]=255; }
            }
            free(nor);
            pk->v = bv; pk->borrowed = 0;   /* own: batch_emit frees it once copied */
            filled++;
        }
    }
    plan_assemble(j);
    return filled;
#else
    (void)j; return 0;
#endif
}

static int bind_prepared(WorldBatchUpload *j, const GLuint *textures,
    GLuint terrain_texture, const unsigned char *modes, int checked) {
    if (!j || !j->prepared || j->count || j->next) return 0;
    /* Texture failures or different material modes use the original grouping.
       checked: the caller compared every mesh's texture/mode with the worker's
       (world_resident.c tex lookup), so the groups and runs are the planned ones. */
    static int verify = -1;
    if (verify < 0) { const char *e = getenv("OPENUG2_ASM_VERIFY"); verify = e && e[0] == '1'; }
    if (checked && verify) printf("worker batch plan: verifying the lookup check\n");
    if (!checked || verify) for (int b=0; b<j->prepared_count; b++) {
        struct PreparedBatch *p=&j->prepared[b]; GLuint first=0;
        for (int k=p->begin; k<p->end; k++) {
            int idx=j->ent[k].idx; const N2Mesh *m=&j->scene->meshes[idx];
            GLuint tex=textures[idx]; if (!tex && m->cat==N2_TERRAIN) tex=terrain_texture;
            int mode=modes ? modes[idx] : N2_DRAW_OPAQUE;
            int group=batch_group(m,mode);
            if (group!=j->ent[k].group || (k>p->begin && tex!=first)) {
                printf("worker batch plan: run %d mesh %s key %08x: group %d vs planned %d, "
                       "texture %u vs run %u\n", b, m->sname, m->texkey, group, j->ent[k].group,
                       (unsigned)tex, (unsigned)first);
                return 0;
            }
            if (k==p->begin) first=tex;
        }
    }
    for (int k=0; k<j->meshes; k++) {
        int idx=j->ent[k].idx; GLuint tex=textures[idx];
        if (!tex && j->scene->meshes[idx].cat==N2_TERRAIN) tex=terrain_texture;
        j->ent[k].key=tex;
    }
    j->modes=modes;
    printf("worker batch plan: %d prepared runs accepted%s\n",j->prepared_count,
           checked ? " (checked at lookup)" : "");
    return 1;
}
int upload_world_batches_bind_prepared(WorldBatchUpload *j, const GLuint *textures,
    GLuint terrain_texture, const unsigned char *modes) {
    return bind_prepared(j, textures, terrain_texture, modes, 0);
}
int upload_world_batches_bind_checked(WorldBatchUpload *j, const GLuint *textures,
    GLuint terrain_texture, const unsigned char *modes) {
    return bind_prepared(j, textures, terrain_texture, modes, 1);
}

/* 11-14 step 2: the packed copy (28 B/vertex, 14-22 MB in a race) was kept for
   the life of the tile only so a later area build could memcpy it again. A
   later build reuses the GPU buffers by hash (85% of batches); the rest now
   rebuild from the mesh, the same bytes. Fading-in meshes keep theirs one more
   build: their batch group changes when the fade ends. Runs between builds
   (finish step, main thread), when the worker is not packing.
   ux0:data/NFSU2/nopktrim.txt keeps the old behaviour. */
#ifdef __vita__
static void **g_trim; static int g_trim_n, g_trim_cap;
#endif
/* Worker, at the start of a build (the main thread is not trimming then):
   frees the copies the last trim detached. */
void render_world_packed_trim_flush(void) {
#ifdef __vita__
    for (int i = 0; i < g_trim_n; i++) free(g_trim[i]);
    g_trim_n = 0;
#endif
}
void render_world_packed_trim(const N2Scene *s) {
    render_world_packed_trim_list(s, NULL, 0);
}
void render_world_packed_trim_list(const N2Scene *s, const int *list, int list_n) {
#ifdef __vita__
    static int off = -1;
    if (off < 0) { FILE *f = fopen("ux0:data/NFSU2/nopktrim.txt", "r"); off = f != NULL; if (f) fclose(f);
        printf("world: CPU batch copies %s after upload\n", off ? "kept (nopktrim.txt)" : "freed"); }
    if (off || !s) return;
    long n = 0, bytes = 0;
    int walk = list ? list_n : s->count;
    for (int w = 0; w < walk; w++) {
        int i = list ? list[w] : w;
        if (i < 0 || i >= s->count) continue;
        const N2Mesh *m = &s->meshes[i];
        N2SharedGeometry *g = m->shared_geometry;
        if (!g || !g->packed || !g->packed_hashed || m->fade_in) continue;
        /* detach now, free on the worker (1500 free() calls cost 6-9 ms on the
           main thread at a finish step: hwlog51_J) */
        if (g_trim_n >= g_trim_cap) {
            int c = g_trim_cap ? g_trim_cap * 2 : 2048;
            void **q = (void **)realloc(g_trim, (size_t)c * sizeof *q);
            if (q) { g_trim = q; g_trim_cap = c; }
        }
        if (g_trim_n < g_trim_cap) g_trim[g_trim_n++] = g->packed;
        else free(g->packed);
        g->packed = NULL;
        n++; bytes += (long)m->nverts * (long)sizeof(BatchedVertex);
    }
    static unsigned calls;
    if ((calls++ & 7) == 0 || bytes > (4l << 20))
        printf("world: %ld CPU batch copies freed (%ld KB)\n", n, bytes >> 10);
#else
    (void)s; (void)list; (void)list_n;
#endif
}

#ifdef __vita__
static int g_pack_keep_selftest;   /* render_batch_reuse_selftest: P1b's drop off */
#endif
/* Same per-vertex result as batch_emit's own loop. */
int render_pack_mesh(const N2Mesh *m, PackedMesh *out) {
    out->v = NULL;
    out->borrowed = 0;
    out->hash_valid = 0;
    if (!m || m->nverts <= 0) return 0;
    N2SharedGeometry *shared = m->shared_geometry;
    if (shared && (shared->packed || shared->packed_hashed)) {
        /* packed may be NULL: freed after its GPU upload (render_world_packed_trim);
           a batch that is not reused then rebuilds from the mesh (batch_emit) */
        out->v = shared->packed; out->borrowed = 1;
        memcpy(out->bb,shared->packed_bb,sizeof out->bb);
        memcpy(out->hash,shared->packed_hash,sizeof out->hash); out->hash_valid=1;
        return 1;
    }
    BatchedVertex *bv = (BatchedVertex *)malloc((size_t)m->nverts * sizeof *bv);
    float *nor = (float *)calloc((size_t)m->nverts * 3, sizeof(float));
    if (!bv || !nor) { free(bv); free(nor); return 0; }
    mesh_normals(m, nor);
    float mn[3] = {1e30f,1e30f,1e30f}, mx[3] = {-1e30f,-1e30f,-1e30f};
    for (int v = 0; v < m->nverts; v++) {
        BatchedVertex *o = &bv[v]; const float *p = m->verts + v*5;
        o->pos[0]=p[0]; o->pos[1]=p[1]; o->pos[2]=p[2];
        o->uv[0]=p[3];  o->uv[1]=p[4];
        BV_SET_NORMAL(o, nor[v*3], nor[v*3+1], nor[v*3+2]);
        if (m->vcol) { o->col[0]=m->vcol[v*4]; o->col[1]=m->vcol[v*4+1];
                       o->col[2]=m->vcol[v*4+2]; o->col[3]=m->vcol[v*4+3]; }
        else { o->col[0]=o->col[1]=o->col[2]=o->col[3]=255; }
        for (int c = 0; c < 3; c++) {
            if (p[c] < mn[c]) mn[c] = p[c];
            if (p[c] > mx[c]) mx[c] = p[c];
        }
    }
    free(nor);
    for (int c = 0; c < 3; c++) { out->bb[c] = mn[c]; out->bb[c+3] = mx[c]; }
    out->v = bv;
#ifdef __vita__
    out->hash[0]=UINT64_C(14695981039346656037); out->hash[1]=UINT64_C(0x6a09e667f3bcc909);
    batch_payload_hash(bv,(size_t)m->nverts*sizeof *bv,out->hash);
    batch_payload_hash(m->idx,(size_t)m->nidx*sizeof *m->idx,out->hash);
    out->hash_valid=1;
#endif
    if (shared) {
        shared->packed = bv;
        memcpy(shared->packed_bb,out->bb,sizeof out->bb);
        memcpy(shared->packed_hash,out->hash,sizeof out->hash);
        shared->packed_hashed = out->hash_valid;
        out->borrowed = 1;
#ifdef __vita__
        /* PERF_PLAN P1b (11-15): the copy was only wanted for its hash (GPU
           reuse) and box; keep those and let it go now, as the trim would
           after upload. A batch the GPU lacks is repacked by the worker
           (render_world_plan_fill) and freed once assembled. Sim census at
           the peak of the double downtown route: 22 MB of these in 9771
           copies. keeppacked.txt / OPENUG2_KEEP_PACKED = keep them. */
        static int drop = -1;
        if (g_pack_keep_selftest) goto keep;   /* the reuse selftest checks the borrowed copy */
        if (drop < 0) {
            FILE *f = fopen("ux0:data/NFSU2/keeppacked.txt", "r");
            drop = !f && !getenv("OPENUG2_KEEP_PACKED");
            if (f) fclose(f);
            printf("world: packed copies %s\n", drop ? "dropped after hashing" : "kept until upload (keeppacked.txt)");
        }
        if (drop && out->hash_valid) { free(bv); shared->packed = NULL; out->v = NULL; }
        keep:;
#endif
    }
    return 1;
}

#ifdef __vita__
static struct { GLuint fbo, tex, depth; int w, h, failed, active; } g_scene;
/* The 3D -> screen upscale touches every display pixel; running it through
   the main fragment program (fog, branches) cost ~8 ms of GPU per frame. */
static const char *BLIT_VS =
    GLSL_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; varying vec2 vUV;\n"
    "void main(){ vUV=aUV; gl_Position=vec4(aPos.xy*2.0-1.0,0.0,1.0); }\n";
static const char *BLIT_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex;\n"
    "void main(){ gl_FragColor=vec4(texture2D(uTex,vUV).rgb,1.0); }\n";

static struct { GLuint fbo, tex, depth; int w, h, failed; } g_refl;
int render_refl_begin(int w, int h, const float clear_rgb[3]) {
    if (g_refl.failed) return 0;
    if (!g_refl.fbo || g_refl.w != w || g_refl.h != h) {
        if (g_refl.fbo) { glDeleteFramebuffers(1, &g_refl.fbo); glDeleteTextures(1, &g_refl.tex);
                          glDeleteRenderbuffers(1, &g_refl.depth); g_refl.fbo = 0; }
        glGenTextures(1, &g_refl.tex);
        glBindTexture(GL_TEXTURE_2D, g_refl.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &g_refl.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_refl.fbo);
        glGenRenderbuffers(1, &g_refl.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, g_refl.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_refl.depth);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_refl.tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("road reflection unavailable (framebuffer incomplete)\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_refl.failed = 1;
            return 0;
        }
        g_refl.w = w; g_refl.h = h;
        printf("road reflection target %dx%d\n", w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_refl.fbo);
    glViewport(0, 0, w, h);
    glClearColor(clear_rgb[0], clear_rgb[1], clear_rgb[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return 1;
}
void render_refl_end(int screen_w, int screen_h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w, screen_h);
}
GLuint render_refl_texture(void) { return g_refl.failed ? 0 : g_refl.tex; }

/* ---- screen + env road reflection (11-15 prototype, the user's options 1+2) ----
   Without Full detail the road reflection does not draw the world again.
   The reflection target is filled by one full-screen pass instead: each
   texel looks along its own view ray mirrored in the road plane and takes
   (1) last frame's picture where that mirrored ray lands on screen -- exact
   for the sky and far lights, near buildings sit a little high (the camera
   height's parallax) -- and (2) the car's environment cube along it where
   the mirrored ray leaves the screen. The road shader is unchanged
   (worldreflect: the target sampled at the pixel's own screen spot). */
static struct { GLuint fbo, tex, down, prog; GLint down_step, uM, uMi, uEnvK; int w, h, failed, want, valid; } g_prev;
static const char *PREV_DOWN_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform vec2 uStep;\n"
    "void main(){ vec3 c=texture2D(uTex,vUV-uStep).rgb+texture2D(uTex,vUV+uStep).rgb\n"
    "  +texture2D(uTex,vUV+vec2(uStep.x,-uStep.y)).rgb+texture2D(uTex,vUV+vec2(-uStep.x,uStep.y)).rgb;\n"
    "  gl_FragColor=vec4(c*0.25,1.0); }\n";
static const char *REFL_SCREEN_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform sampler2D uScene;\n"
    "uniform mat3 uM; uniform mat3 uMi; uniform float uEnvK;\n"
    "vec2 cubeuv(vec3 r){\n"
    "  vec3 a=abs(r); vec2 uv; float fcol; float frow;\n"
    "  if(a.x>=a.y && a.x>=a.z){ uv=vec2(r.x>0.0?-r.y:r.y, r.z)/a.x; fcol=r.x>0.0?0.0:1.0; frow=0.0; }\n"
    "  else if(a.y>=a.z){ uv=vec2(r.y>0.0?r.x:-r.x, r.z)/a.y; fcol=r.y>0.0?2.0:0.0; frow=r.y>0.0?0.0:1.0; }\n"
    "  else { uv=vec2(r.z>0.0?-r.x:r.x, r.y)/a.z; fcol=r.z>0.0?1.0:2.0; frow=1.0; }\n"
    "  uv=uv*(0.5*62.0/64.0)+0.5;\n"
    "  return vec2((fcol+uv.x)*(1.0/3.0),(frow+uv.y)*0.5); }\n"
    "void main(){\n"
    "  vec3 d=uMi*vec3(vUV*2.0-1.0,1.0);\n"
    "  vec3 m=vec3(d.x,d.y,-d.z);\n"
    "  vec3 e=texture2D(uScene,cubeuv(m)).rgb*uEnvK;\n"
    "  vec3 c=uM*m; float w=0.0; vec3 s=e;\n"
    "  if(c.z>0.0){ vec2 q=c.xy/c.z*0.5+0.5;\n"
    "    w=clamp((1.0-q.y)*10.0,0.0,1.0)*clamp(q.y*20.0,0.0,1.0)*clamp(q.x*20.0,0.0,1.0)*clamp((1.0-q.x)*20.0,0.0,1.0);\n"
    "    s=texture2D(uTex,clamp(q,0.0,1.0)).rgb; }\n"
    "  gl_FragColor=vec4(mix(e,s,w),1.0); }\n";
static GLuint bloom_program_vs(const char *vs, const char *fs, const char *name);
void render_prev_want(int on) { g_prev.want = on; if (!on) g_prev.valid = 0; }
int render_prev_valid(void) { return g_prev.valid; }
/* render_scene_end: last frame's picture at reflection size */
static void prev_capture(GpuMesh *quad) {
    if (!g_prev.want || g_prev.failed || !quad || !g_scene.tex) return;
    if (!g_prev.fbo) {
        g_prev.w = 240; g_prev.h = 136;
        glGenTextures(1, &g_prev.tex);
        glBindTexture(GL_TEXTURE_2D, g_prev.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_prev.w, g_prev.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &g_prev.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_prev.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_prev.tex, 0);
        g_prev.down = bloom_program_vs(BLIT_VS, PREV_DOWN_FS, "reflection downsample");
        g_prev.prog = bloom_program_vs(BLIT_VS, REFL_SCREEN_FS, "screen reflection");
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || !g_prev.down || !g_prev.prog) {
            printf("screen reflection unavailable\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_prev.failed = 1;
            return;
        }
        g_prev.down_step = glGetUniformLocation(g_prev.down, "uStep");
        g_prev.uM = glGetUniformLocation(g_prev.prog, "uM");
        g_prev.uMi = glGetUniformLocation(g_prev.prog, "uMi");
        g_prev.uEnvK = glGetUniformLocation(g_prev.prog, "uEnvK");
        glUseProgram(g_prev.prog);
        glUniform1i(glGetUniformLocation(g_prev.prog, "uScene"), RENDER_ENV_UNIT);
        printf("screen reflection: %dx%d last-frame copy + env cube\n", g_prev.w, g_prev.h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_prev.fbo);
    glViewport(0, 0, g_prev.w, g_prev.h);
    glUseProgram(g_prev.down);
    glUniform2f(g_prev.down_step, 0.75f / (float)g_scene.w, 0.75f / (float)g_scene.h);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_scene.tex);
    draw_gpumesh(quad);
    g_prev.valid = 1;
}
/* Into the bound reflection target (render_refl_begin). VP = projection x
   view, column-major; only its rotation part is used (rays, not points). */
int render_refl_screen(GpuMesh *quad, const float VP[16], float env_k) {
    if (!g_prev.valid || !g_prev.prog || !quad) return 0;
    static const int rs[3] = { 0, 1, 3 };   /* clip x, y, w */
    double M[3][3];   /* M[row][col]: direction (col) -> clip (row) */
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) M[r][c] = VP[c * 4 + rs[r]];
    double det = M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1]) - M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0])
               + M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]);
    if (fabs(det) < 1e-12) return 0;
    double I[3][3];
    I[0][0] = (M[1][1]*M[2][2]-M[1][2]*M[2][1])/det; I[0][1] = (M[0][2]*M[2][1]-M[0][1]*M[2][2])/det; I[0][2] = (M[0][1]*M[1][2]-M[0][2]*M[1][1])/det;
    I[1][0] = (M[1][2]*M[2][0]-M[1][0]*M[2][2])/det; I[1][1] = (M[0][0]*M[2][2]-M[0][2]*M[2][0])/det; I[1][2] = (M[0][2]*M[1][0]-M[0][0]*M[1][2])/det;
    I[2][0] = (M[1][0]*M[2][1]-M[1][1]*M[2][0])/det; I[2][1] = (M[0][1]*M[2][0]-M[0][0]*M[2][1])/det; I[2][2] = (M[0][0]*M[1][1]-M[0][1]*M[1][0])/det;
    float mc[9], ic[9];   /* column-major for glUniformMatrix3fv */
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) { mc[c * 3 + r] = (float)M[r][c]; ic[c * 3 + r] = (float)I[r][c]; }
    GLint cur = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glUseProgram(g_prev.prog);
    glUniformMatrix3fv(g_prev.uM, 1, GL_FALSE, mc);
    glUniformMatrix3fv(g_prev.uMi, 1, GL_FALSE, ic);
    glUniform1f(g_prev.uEnvK, env_k);
    glActiveTexture(GL_TEXTURE0 + RENDER_ENV_UNIT); glBindTexture(GL_TEXTURE_2D, render_env_texture());
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_prev.tex);
    draw_gpumesh(quad);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (blend) glEnable(GL_BLEND);
    glUseProgram((GLuint)cur);
    return 1;
}

/* Car environment cube (car.fx EnvMapTexture; the game renders a 128 px
   dynamic cube). Here: six 64 px faces in one 192x128 target, laid out
   +X -X +Y / -Y +Z -Z, each face rendered into its own viewport. */
static struct { GLuint fbo, tex, depth; int failed, ready; } g_env;
int render_env_begin(int face, const float clear_rgb[3]) {
    if (g_env.failed) return 0;
    if (!g_env.fbo) {
        glGenTextures(1, &g_env.tex);
        glBindTexture(GL_TEXTURE_2D, g_env.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 192, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &g_env.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_env.fbo);
        glGenRenderbuffers(1, &g_env.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, g_env.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 192, 128);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_env.depth);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_env.tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("car environment map unavailable (framebuffer incomplete)\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_env.failed = 1;
            return 0;
        }
        /* start from the fog colour everywhere until the faces fill in */
        glViewport(0, 0, 192, 128);
        glClearColor(clear_rgb[0], clear_rgb[1], clear_rgb[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        printf("car environment map 6x64 px (192x128 atlas)\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_env.fbo);
    int x = (face % 3) * 64, y = (face / 3) * 64;
    glViewport(x, y, 64, 64);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, 64, 64);
    glClearColor(clear_rgb[0], clear_rgb[1], clear_rgb[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return 1;
}
void render_env_end(int screen_w, int screen_h) {
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w, screen_h);
    g_env.ready = 1;
}
/* Until the cube has been rendered, a neutral 1x1 texel: sampling texture 0
   on the Vita reads whatever memory is there (lime-green trim). */
static GLuint g_env_static; static int g_env_static_on;
void render_env_static(const unsigned char *rgba, int w, int h) {
    if (!rgba) {
        if (g_env_static) glDeleteTextures(1, &g_env_static);
        g_env_static = 0;
        return;
    }
    if (!g_env_static) glGenTextures(1, &g_env_static);
    glBindTexture(GL_TEXTURE_2D, g_env_static);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
void render_env_use_static(int on) { g_env_static_on = on; }
GLuint render_env_texture(void) {
    if (g_env_static_on && g_env_static) return g_env_static;
    if (!g_env.failed && g_env.ready) return g_env.tex;
    static GLuint blank;
    if (!blank) {
        static const unsigned char px[4] = { 64, 64, 64, 255 };
        glGenTextures(1, &blank);
        glBindTexture(GL_TEXTURE_2D, blank);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    return blank;
}
/* View matrix for cube face f (eye at e): rows right, up, -forward. */
void render_env_view(int face, const float e[3], float *V) {
    static const float F[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    static const float U[6][3] = { {0,0,1}, {0,0,1}, {0,0,1}, {0,0,1}, {0,1,0}, {0,1,0} };
    const float *f = F[face], *u = U[face];
    float r[3] = { f[1]*u[2]-f[2]*u[1], f[2]*u[0]-f[0]*u[2], f[0]*u[1]-f[1]*u[0] };
    float m[16] = { r[0], u[0], -f[0], 0,  r[1], u[1], -f[1], 0,  r[2], u[2], -f[2], 0,
                    -(r[0]*e[0]+r[1]*e[1]+r[2]*e[2]), -(u[0]*e[0]+u[1]*e[1]+u[2]*e[2]),
                    f[0]*e[0]+f[1]*e[1]+f[2]*e[2], 1 };
    memcpy(V, m, sizeof m);
}
static GLuint g_blit_prog;
static GLuint blit_program(void);
void render_debug_texture(GLuint tex, GpuMesh *quad, int x, int y, int w, int h,
                          int screen_w, int screen_h) {
    GLuint p = blit_program();
    if (!tex || !quad || p == (GLuint)-1) return;
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glViewport(x, y, w, h);
    glUseProgram(p);
    glBindTexture(GL_TEXTURE_2D, tex);
    draw_gpumesh(quad);
    glViewport(0, 0, screen_w, screen_h);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND);
}

static GLuint blit_program(void) {
    if (!g_blit_prog) {
        GLuint p = glCreateProgram();
        glAttachShader(p, compile(GL_VERTEX_SHADER, BLIT_VS));
        glAttachShader(p, compile(GL_FRAGMENT_SHADER, BLIT_FS));
        glBindAttribLocation(p, 0, "aPos");
        glBindAttribLocation(p, 1, "aUV");
        glLinkProgram(p);
        GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        g_blit_prog = ok ? p : (GLuint)-1;
        printf("blit program: %s\n", ok ? "ok" : "link failed");
    }
    return g_blit_prog;
}

/* ---- NFSU2 bloom (overbright.fx + filter.fx chain, decoded from SPEED2.EXE) ----
 * Original, per frame: S = scene at half res; A = overbright(S); twice
 * { B = H-blur(A); A = V-blur(B) } with taps (1/3 at +-1, 1/6 at +-3 texels);
 * A -= S * sub (reverse-subtract, clamped); glow buffer G = G * per-channel
 * decay (0x78, 0x5a, 0xaa)/255 + A * add; screen += G. The overbright
 * offset and add follow an eased level each frame (bloom_levels: 0x5bc180
 * / 0x5bc100); sub is 0xB0/255 in game (0x8654a4 == 6), else 0xA0/255.
 * Here S is a bilinear tap of the full-res scene (= the 2x2 box the copy made),
 * the subtract and the G update ride on the last blur pass, and G ping-pongs
 * between two half-res targets (the original G is full res and also receives
 * the light-glow sprites; ours holds bloom only, so half res loses nothing). */
static const char *BLOOM_OB_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform float uObOfs;\n"
    "void main(){\n"
    "  vec3 c=clamp((texture2D(uTex,vUV).rgb-uObOfs)*4.0,0.0,1.0);\n"     /* OverBrightOffset 0x7ff10c/255*0.5 */
    "  c*=dot(c,vec3(0.5882353));\n"                                       /* OverBrightGreyScale 150/255 */
    "  gl_FragColor=vec4(c*c,1.0);\n"
    "}\n";
#define BLOOM_BLUR_SRC \
    "  vec3 b=(texture2D(uTex,vUV-uStep).rgb+texture2D(uTex,vUV+uStep).rgb)*0.3333333\n" \
    "       +(texture2D(uTex,vUV-3.0*uStep).rgb+texture2D(uTex,vUV+3.0*uStep).rgb)*0.1666667;\n"
static const char *BLOOM_BLUR_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform vec2 uStep;\n"
    "void main(){\n" BLOOM_BLUR_SRC "  gl_FragColor=vec4(b,1.0);\n}\n";
static const char *BLOOM_GLOW_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform sampler2D uScene; uniform sampler2D uGlow;\n"
    "uniform vec2 uStep; uniform float uSub; uniform float uAdd;\n"
    "void main(){\n" BLOOM_BLUR_SRC
    "  vec3 a=max(b-texture2D(uScene,vUV).rgb*uSub,0.0);\n"
    "  gl_FragColor=vec4(texture2D(uGlow,vUV).rgb*vec3(0.4705882,0.3529412,0.6666667)+a*uAdd,1.0);\n"
    "}\n";
/* The same chain in two passes. The blur is separable, so H then V equals one
 * 4x4 tap grid with weights wi*wj (1/3 at +-1, 1/6 at +-3 texels; 1/9, 1/18,
 * 1/36). Every tap lands on a half-res texel centre, so applying the
 * overbright per tap (clamped, as the 8-bit target did) is the overbright
 * pass sampled exactly where the first blur read it. Pass 1 = overbright + H
 * + V, pass 2 = H + V + glow update: 2 GPU scenes instead of 5. */
#define BLOOM_GRID(T) \
    "  vec3 b=(" T("-1.0,-1.0") "+" T("1.0,-1.0") "+" T("-1.0,1.0") "+" T("1.0,1.0") ")*0.1111111\n" \
    "   +(" T("-1.0,-3.0") "+" T("1.0,-3.0") "+" T("-1.0,3.0") "+" T("1.0,3.0") \
    "    +" T("-3.0,-1.0") "+" T("3.0,-1.0") "+" T("-3.0,1.0") "+" T("3.0,1.0") ")*0.0555556\n" \
    "   +(" T("-3.0,-3.0") "+" T("3.0,-3.0") "+" T("-3.0,3.0") "+" T("3.0,3.0") ")*0.0277778;\n"
#define BLOOM_TAP_OB(o) "ob(texture2D(uTex,vUV+vec2(" o ")*uStep).rgb)"
#define BLOOM_TAP(o) "texture2D(uTex,vUV+vec2(" o ")*uStep).rgb"
static const char *BLOOM_OBBLUR_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform vec2 uStep; uniform float uObOfs;\n"
    "vec3 ob(vec3 x){ vec3 c=clamp((x-uObOfs)*4.0,0.0,1.0);\n"
    "  c*=dot(c,vec3(0.5882353)); return clamp(c*c,0.0,1.0); }\n"
    "void main(){\n" BLOOM_GRID(BLOOM_TAP_OB) "  gl_FragColor=vec4(b,1.0);\n}\n";
static const char *BLOOM_GLOW2_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform sampler2D uScene; uniform sampler2D uGlow;\n"
    "uniform vec2 uStep; uniform float uSub; uniform float uAdd;\n"
    "void main(){\n" BLOOM_GRID(BLOOM_TAP)
    "  vec3 a=max(b-texture2D(uScene,vUV).rgb*uSub,0.0);\n"
    "  gl_FragColor=vec4(texture2D(uGlow,vUV).rgb*vec3(0.4705882,0.3529412,0.6666667)+a*uAdd,1.0);\n"
    "}\n";
/* The chain again as the original's separable passes, but with every tap
 * a non-dependent read: the vertex shader hands the fragment shader the
 * four tap coordinates (-3, -1, +1, +3 steps along uStep) as varyings, so
 * the GPU prefetches them instead of stalling on 16-18 computed-coordinate
 * reads per pixel (the fused 2-pass chain cost ~6 ms a frame on the Vita,
 * GPU probe 10-12). Pass 1 = overbright + H (per tap, from the full-res
 * scene at half-res texel centres = the 2x2 box), 2 = V, 3 = H, 4 = V +
 * the scene subtract + the glow update: 4 small scenes, 18 reads. Each
 * pass writes an 8-bit target as the original's did. */
static const char *BLOOM_SEP_VS =
    GLSL_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; uniform vec2 uStep;\n"
    "varying vec2 vUV; varying vec2 vT0; varying vec2 vT1; varying vec2 vT2; varying vec2 vT3;\n"
    "void main(){ vUV=aUV; vT0=aUV-3.0*uStep; vT1=aUV-uStep; vT2=aUV+uStep; vT3=aUV+3.0*uStep;\n"
    "  gl_Position=vec4(aPos.xy*2.0-1.0,0.0,1.0); }\n";
#define BLOOM_SEP_DECL \
    "varying vec2 vUV; varying vec2 vT0; varying vec2 vT1; varying vec2 vT2; varying vec2 vT3;\n"
#define BLOOM_SEP_SUM(T) \
    "  vec3 b=(" T("vT1") "+" T("vT2") ")*0.3333333+(" T("vT0") "+" T("vT3") ")*0.1666667;\n"
#define BLOOM_SEP_TAP(v) "texture2D(uTex," v ").rgb"
#define BLOOM_SEP_TAP_OB(v) "ob(texture2D(uTex," v ").rgb)"
static const char *BLOOM_SEP_OB_FS =
    GLSL_HEADER BLOOM_SEP_DECL
    "uniform sampler2D uTex; uniform float uObOfs;\n"
    "vec3 ob(vec3 x){ vec3 c=clamp((x-uObOfs)*4.0,0.0,1.0);\n"
    "  c*=dot(c,vec3(0.5882353)); return clamp(c*c,0.0,1.0); }\n"
    "void main(){\n" BLOOM_SEP_SUM(BLOOM_SEP_TAP_OB) "  gl_FragColor=vec4(b,1.0);\n}\n";
static const char *BLOOM_SEP_BLUR_FS =
    GLSL_HEADER BLOOM_SEP_DECL
    "uniform sampler2D uTex;\n"
    "void main(){\n" BLOOM_SEP_SUM(BLOOM_SEP_TAP) "  gl_FragColor=vec4(b,1.0);\n}\n";
static const char *BLOOM_SEP_GLOW_FS =
    GLSL_HEADER BLOOM_SEP_DECL
    "uniform sampler2D uTex; uniform sampler2D uScene; uniform sampler2D uGlow;\n"
    "uniform float uSub; uniform float uAdd;\n"
    "void main(){\n" BLOOM_SEP_SUM(BLOOM_SEP_TAP)
    "  vec3 a=max(b-texture2D(uScene,vUV).rgb*uSub,0.0);\n"
    "  gl_FragColor=vec4(texture2D(uGlow,vUV).rgb*vec3(0.4705882,0.3529412,0.6666667)+a*uAdd,1.0);\n"
    "}\n";
/* Visual treatment (screenfilter.fx ps_1_4 technique, 3 passes), in-game
 * values: FilterBlend t = 0.2 (no camera FX), TintColor = (0.5,0.5,0.5)*0.25t
 * with w = 0.2 left by the front end, SaturationScale 0.4, ramp bias 0.45,
 * gain curves k = 0.5 + (0.2-0.5)t = 0.44 around pivot 0.4. Each pass wrote an
 * 8-bit target, hence the clamps. The game applies it before adding the glow
 * buffer. Gain curve (0x5bbdb0): m = (1/k-2)(1-x/s);
 * x < s ? x/(m+1) : (m-x)/(m-1). */
/* Depth of field composite (depth_of_field.efx ps_2_0, drawn by 0x5cccd0
 * after the screen flash, before the visual treatment): s = the sharp frame
 * with the depth in alpha, b = its blurred copy (half res, box + 2 x H/V);
 * w = sat8(d * F + O1) + sat8(O2 - d * F) with sat8 = clamp to [0, 0.8]
 * (DofFalloff F, DofOffset1 O1, DofOffset2 O2 from 0x5d2850). The
 * letterbox (0x5cbdc0, after the 3D post and before the FE) blacks out the
 * rows outside |y - 0.5| <= uLbH. */
#define FIN_DOF_DECL "uniform sampler2D uBlur; uniform vec3 uDof;\n"
#define FIN_DOF_MIX \
    "  { float w=clamp(s.a*uDof.x+uDof.y,0.0,0.8)+clamp(uDof.z-s.a*uDof.x,0.0,0.8);\n" \
    "    s.rgb=mix(s.rgb,texture2D(uBlur,vUV).rgb,w); }\n"
#define FIN_OUT \
    "  gl_FragColor=vec4((c+texture2D(uGlow,vUV).rgb*uBloom)*step(abs(vUV.y-0.5),uLbH),1.0);\n" \
    "}\n"
/* the speed blur's blend over the frame (0x5cc460: BLENDFACTOR /
   INVBLENDFACTOR, factor = its strength), before the flash and the DOF */
#define FIN_SPEED_DECL "uniform sampler2D uSpeedTex; uniform float uSpeed;\n"
#define FIN_SPEED_MIX "  if(uSpeed>0.0) s.rgb=mix(s.rgb,texture2D(uSpeedTex,vUV).rgb,uSpeed);\n"
#define BLOOM_FINAL_LUT(DECL, MIX) \
    GLSL_HEADER \
    "varying vec2 vUV; uniform sampler2D uTex; uniform sampler2D uGlow; uniform sampler2D uLut;\n" \
    "uniform float uVT; uniform float uBloom; uniform float uLift; uniform vec3 uFlash; uniform float uLbH;\n" \
    FIN_SPEED_DECL DECL \
    /* uLut: r = gain curve, g = blue ramp, at x*255/256 + 0.5/256 */ \
    "void main(){\n" \
    "  vec4 s=texture2D(uTex,vUV);\n" \
    FIN_SPEED_MIX MIX \
    "  vec3 c=min(s.rgb*uFlash,1.0);\n" \
    "  if(uVT>0.5){\n" \
    "    c.b=texture2D(uLut,vec2(c.b*0.99609375+0.001953125,0.5)).g;\n"   /* RAMPMAP (1x256, indexed by blue) */ \
    "    c=mix(c,vec3(s.a),uLift);\n"                                       /* TintColor.w */ \
    "    c=clamp(c+clamp(1.0-(max(c.r,c.g)-c.b),0.0,1.0)*0.01,0.0,1.0);\n" \
    "    vec3 q=c*0.99609375+0.001953125;\n" \
    "    vec3 g=vec3(texture2D(uLut,vec2(q.r,0.5)).r,texture2D(uLut,vec2(q.g,0.5)).r,texture2D(uLut,vec2(q.b,0.5)).r);\n" \
    "    float w=texture2D(uLut,vec2(dot(q,vec3(0.3,0.59,0.11)),0.5)).r;\n" /* GAINMAP alpha */ \
    "    c=mix(g,c,w);\n" \
    "  }\n" \
    FIN_OUT
/* The same treatment with the curves as polynomials: five dependent LUT reads
   per pixel made this full-screen pass ~5 ms of GPU on the Vita. Least-squares
   cubics over the 256 8-bit inputs: blue ramp max error 0.03/255, gain curve
   split at its pivot 0.4 (continuous there) max error 0.02/255 below and
   0.09/255 above, all under the LUT's own 8-bit rounding. r, g, b and the
   luma weight go through the gain curve as one vec4. */
#define BLOOM_FINAL_POLY(DECL, MIX) \
    GLSL_HEADER \
    "varying vec2 vUV; uniform sampler2D uTex; uniform sampler2D uGlow;\n" \
    "uniform float uVT; uniform float uBloom; uniform float uLift; uniform vec3 uFlash; uniform float uLbH;\n" \
    FIN_SPEED_DECL DECL \
    "void main(){\n" \
    "  vec4 s=texture2D(uTex,vUV);\n" \
    FIN_SPEED_MIX MIX \
    "  vec3 c=min(s.rgb*uFlash,1.0);\n" \
    "  if(uVT>0.5){\n" \
    "    c.b=((0.040048*c.b+0.139653)*c.b+0.820287)*c.b-0.000106;\n"       /* RAMPMAP */ \
    "    c=mix(c,vec3(s.a),uLift);\n" \
    "    c=clamp(c+clamp(1.0-(max(c.r,c.g)-c.b),0.0,1.0)*0.01,0.0,1.0);\n" \
    "    vec4 q=vec4(c,dot(c,vec3(0.3,0.59,0.11)));\n" \
    "    vec4 lo=((0.360683*q+0.382482)*q+0.789274)*q-0.000070;\n" \
    "    vec4 hi=((0.321546*q-1.239069)*q+2.232983)*q-0.315162;\n" \
    "    vec4 g=clamp(mix(lo,hi,step(0.4,q)),0.0,1.0);\n"                  /* GAINMAP */ \
    "    c=mix(g.rgb,c,g.a);\n" \
    "  }\n" \
    FIN_OUT
static const char *BLOOM_FINAL_FS = BLOOM_FINAL_LUT("", "");
static const char *BLOOM_FINAL_DOF_FS = BLOOM_FINAL_LUT(FIN_DOF_DECL, FIN_DOF_MIX);
static const char *BLOOM_FINAL_POLY_FS = BLOOM_FINAL_POLY("", "");
static const char *BLOOM_FINAL_POLY_DOF_FS = BLOOM_FINAL_POLY(FIN_DOF_DECL, FIN_DOF_MIX);
/* the DOF blur passes: the bloom's tap grid on the plain frame */
static const char *DOF_BLUR_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform vec2 uStep;\n"
    "void main(){\n" BLOOM_GRID(BLOOM_TAP) "  gl_FragColor=vec4(b,1.0);\n}\n";
/* Speed / nitrous motion blur (0x5cc460, filter.fx through 0x5c5c00): 4 taps
 * of weight 0.25, tap i at uv - i * offset, the offset given per corner
 * (x: o0 on the left edge, o2 on the right; y: o1 at the top, o3 at the
 * bottom, D3D v down) and interpolated across the quad -- a zoom along the
 * camera's motion. Pass 2 repeats it at 4x the offsets (16 taps in all).
 * Tap coordinates come from the vertex shader (non-dependent reads). */
static const char *SPEED_VS =
    GLSL_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; uniform vec4 uOfs;\n"
    "varying vec2 vT0; varying vec2 vT1; varying vec2 vT2; varying vec2 vT3;\n"
    "void main(){\n"
    "  vec2 o=vec2(mix(uOfs.x,uOfs.z,aUV.x),-mix(uOfs.y,uOfs.w,1.0-aUV.y));\n"
    "  vT0=aUV; vT1=aUV-o; vT2=aUV-2.0*o; vT3=aUV-3.0*o;\n"
    "  gl_Position=vec4(aPos.xy*2.0-1.0,0.0,1.0); }\n";
static const char *SPEED_FS =
    GLSL_HEADER
    "varying vec2 vT0; varying vec2 vT1; varying vec2 vT2; varying vec2 vT3; uniform sampler2D uTex;\n"
    "void main(){ gl_FragColor=vec4((texture2D(uTex,vT0).rgb+texture2D(uTex,vT1).rgb\n"
    "  +texture2D(uTex,vT2).rgb+texture2D(uTex,vT3).rgb)*0.25,1.0); }\n";

static struct {
    int state;                 /* 0 untried, 1 ready, -1 unavailable */
    int w, h, cur;             /* half-res size; G[cur] is the latest glow */
    GLuint fbo[4], tex[4];     /* A, B, G0, G1 */
    GLuint ob, blur, glow, obblur, glow2;   /* obblur+glow2: the fused chain */
    GLint blur_step, glow_step, obblur_step, glow2_step;
    struct { GLuint p; GLint vt, bloom, lift, flash, lbh, dof, speed; } fin[2];   /* plain, depth of field */
    GLuint speed; GLint speed_ofs;   /* the speed blur passes */
    GLuint dofblur; GLint dofblur_step;
    GLuint lut;
    GLuint sob, sblur, sglow;  /* the separable chain (non-dependent taps) */
    GLint sob_step, sblur_step, sglow_step;
} g_bloom;
static int g_bloom_on = -1, g_vt_on = -1, g_vt_lift = 0;
/* the scene camera's depth of field and letterbox for this frame */
/* scene: the depth alpha is being written now; frame: this frame's 3D has it */
static struct { int on, scene, frame; float F, O1, O2; float lbh; } g_dof = { 0, 0, 0, 0, 0, 0, 1.0f };
void render_set_dof(float centre, float range)
{
    /* 0x5d2850: off when both are 0; centre < 0 -> 0, range < 0 -> 8; depth
       in the alpha is viewZ / 1024, DofFalloff F = 1 / (range * 0.01953125),
       DofOffset1 = -(c / 1024 + r / 2048) F, DofOffset2 = (c / 1024 - r / 2048) F */
    if (centre == 0.0f && range == 0.0f) { g_dof.on = 0; return; }
    float c = centre < 0.0f ? 0.0f : centre, r = range < 0.0f ? 8.0f : range;
    if (r < 1e-4f) r = 1e-4f;   /* the original divides by 0 here (x87 inf) */
    float h = r * 0.00048828125f, m = c * 0.0009765625f;
    g_dof.F = 1.0f / (r * 0.01953125f);
    g_dof.O1 = -(m + h) * g_dof.F;
    g_dof.O2 = (m - h) * g_dof.F;
    /* The composite's blur weight is clamp(a F + O1, 0, .8) + clamp(O2 - a F,
       0, .8) over the depth alpha a in 0..1 -- piecewise linear, so its
       largest value is at a = 0 or a = 1. When that stays under half a step
       of the 8-bit output the effect changes no pixel (the race intros ask
       for centre 50 km, range 100 km: weight 0 everywhere), so its two
       half-res blur scenes and the depth-alpha writes are skipped. */
    float w0 = fminf(fmaxf(g_dof.O1, 0.0f), .8f) + fminf(fmaxf(g_dof.O2, 0.0f), .8f);
    float w1 = fminf(fmaxf(g_dof.F + g_dof.O1, 0.0f), .8f) + fminf(fmaxf(g_dof.O2 - g_dof.F, 0.0f), .8f);
    g_dof.on = fmaxf(w0, w1) >= 0.002f;
}
static int g_lb_scissor;
void render_set_letterbox(float lb)
{
    /* 0x5cbdc0: two black bars of ftol(lb * 480) lines of the 480-line frame */
    int h = lb > 0.0f ? (int)(lb * 480.0f) : 0;
    g_dof.lbh = h > 0 ? 0.5f - (float)h / 480.0f : 1.0f;
    /* The composite blacks the bars out (uLbH), so the 3D pass need not
       draw under them: a scissor on the band in between (vitaGL clips whole
       tiles outside it). Set after the scene's clear, dropped at its end;
       2 rows of margin for the bloom / blur taps at the edge. */
    if (g_scene.active && h > 0) {
        int H = g_scene.h;
        int y0 = (int)((0.5f - g_dof.lbh) * (float)H) - 2, y1 = (int)((0.5f + g_dof.lbh) * (float)H + 0.999f) + 2;
        if (y0 < 0) y0 = 0;
        if (y1 > H) y1 = H;
        glScissor(0, y0, g_scene.w, y1 - y0);
        glEnable(GL_SCISSOR_TEST);
        g_lb_scissor = 1;
    }
}
#ifdef __vita__
void vita_set_dof(int on);
#endif
void render_dof_scene(int on)
{
    /* the depth alpha needs the shim's per-draw switch (Vita) and the
       post-process path that reads it */
#ifdef __vita__
    g_dof.scene = on && g_dof.on && g_scene.active;
    if (g_dof.scene) g_dof.frame = 1;
    vita_set_dof(g_dof.scene);
#else
    (void)on; g_dof.scene = 0;
#endif
}
int render_dof_writes(void) { return g_dof.scene; }
/* the screen flash (screen effect slot 0, drawn by 0x5c7860 before the
   visual treatment): the frame times 2 x colour / 255 per channel. Only
   the post-process composite applies it (not the plain blit). */
static float g_flash[3] = { 1.0f, 1.0f, 1.0f };
void render_set_flash(const float mul[3]) {
    for (int i = 0; i < 3; i++) g_flash[i] = mul ? mul[i] : 1.0f;
}
/* the speed blur for this frame: strength (0 = off) and the four corner
   offsets of the first pass (0x5cc460); the bloom level f of screen effect
   slot 4 (0x5bc180, the nitrous kick) */
static struct { float k, o[4]; int on; } g_speed = { 0, { 0, 0, 0, 0 }, -1 };
static float g_bloom_f;
void render_set_speedblur(float k, const float o[4]) {
    g_speed.k = k;
    for (int i = 0; i < 4; i++) g_speed.o[i] = o ? o[i] : 0.0f;
}
void render_set_bloom_level(float f) { g_bloom_f = f; }

/* bloom and the visual treatment both post-process the finished 3D frame */
static int bloom_wanted(void) {
    if (g_bloom_on < 0) {
        FILE *f = fopen("ux0:data/NFSU2/nobloom.txt", "r");
        g_bloom_on = !f; if (f) fclose(f);
        f = fopen("ux0:data/NFSU2/novt.txt", "r");
        g_vt_on = !f; if (f) fclose(f);
        /* TintColor.w = 0.2 mixes the frame ALPHA into every pixel; on PC the
           scene alpha evidently is not written (side-by-side on HW 10-09: with
           the lift blacks turn milky grey, without it matches the game), so
           the lift is opt-in for comparisons */
        f = fopen("ux0:data/NFSU2/vtlift.txt", "r");
        g_vt_lift = f != NULL; if (f) fclose(f);
        f = fopen("ux0:data/NFSU2/nospeedblur.txt", "r");
        g_speed.on = !f; if (f) fclose(f);
        printf("speed blur: %s\n", g_speed.on ? "on" : "off (nospeedblur.txt)");
        printf("bloom: %s; visual treatment: %s\n", g_bloom_on ? "on" : "off (nobloom.txt)",
               g_vt_on ? (g_vt_lift ? "on, with alpha lift (vtlift.txt)" : "on") : "off (novt.txt)");
    }
    return (g_bloom_on || g_vt_on) && g_bloom.state >= 0;
}

static GLuint bloom_program_vs(const char *vs, const char *fs, const char *name);
static GLuint bloom_program(const char *fs, const char *name) { return bloom_program_vs(BLIT_VS, fs, name); }
static GLuint bloom_program_vs(const char *vs, const char *fs, const char *name) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUV");
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { printf("bloom: %s program failed to link\n", name); return 0; }
    glUseProgram(p);
    GLint u;
    if ((u = glGetUniformLocation(p, "uTex")) >= 0) glUniform1i(u, 0);
    if ((u = glGetUniformLocation(p, "uScene")) >= 0) glUniform1i(u, 1);
    if ((u = glGetUniformLocation(p, "uGlow")) >= 0) glUniform1i(u, 2);
    if ((u = glGetUniformLocation(p, "uLut")) >= 0) glUniform1i(u, 5);
    if ((u = glGetUniformLocation(p, "uSpeedTex")) >= 0) glUniform1i(u, 4);
    return p;
}

/* 0x5bc180, once a frame from the renderer (0x5d2b5a): level x = 0.2 +
 * 0.8 f, f = 0.45 outside the game; in game (0x8654a4 == 6) 0, or screen
 * effect slot 4's k (+0x1e0) while the slot is active (+0x3c) -- the
 * nitrous preset's kick (render_set_bloom_level). 0x5bc100 eases s = 0.8 s + 0.2 x and sets, through ftol, the glow add
 * 0x7ff114 = 10 + (72 - 10) s and the overbright offset 0x7ff10c = 216 +
 * (62 - 216) s (both /255; the offset then x 0.5, 0x5c641b). In a race s
 * settles at 0.2: add 22, offset 185 -- not the start values 170 / 205,
 * nor the 80 the add takes outside the game (0x5ccb59). The 3D world (and
 * so this chain) runs only in game here; s starts where the front end
 * left it (0.56). */
static float g_bloom_s = 0.56f;
static void bloom_levels(float *ofs, float *sub, float *add) {
    const float x = 0.2f + (1.0f - 0.2f) * g_bloom_f;   /* f: slot 4's k while it is active, else 0 */
    g_bloom_s = (1.0f - 0.2f) * g_bloom_s + 0.2f * x;
    int a = (int)((float)(72 - 10) * g_bloom_s + 10.0f);
    int o = (int)((float)(62 - 216) * g_bloom_s + 216.0f);
    *add = (float)a / 255.0f;
    *ofs = (float)o / 255.0f * 0.5f;
    *sub = (float)0xB0 / 255.0f;
}

static int bloom_setup(int sw, int sh) {
    g_bloom.state = -1;
    g_bloom.w = sw / 2; g_bloom.h = sh / 2;
    for (int i = 0; i < 4; i++) {
        glGenTextures(1, &g_bloom.tex[i]);
        glBindTexture(GL_TEXTURE_2D, g_bloom.tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_bloom.w, g_bloom.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &g_bloom.fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, g_bloom.fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_bloom.tex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("bloom: unavailable (framebuffer %d incomplete)\n", i);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return 0;
        }
        glViewport(0, 0, g_bloom.w, g_bloom.h);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);   /* G starts dark */
    }
    g_bloom.ob = bloom_program(BLOOM_OB_FS, "overbright");
    g_bloom.blur = bloom_program(BLOOM_BLUR_FS, "blur");
    g_bloom.glow = bloom_program(BLOOM_GLOW_FS, "glow");
    {   int lut = 0;   /* ux0:data/NFSU2/vtlut.txt: the LUT treatment, for A/B */
#ifdef __vita__
        FILE *fl = fopen("ux0:data/NFSU2/vtlut.txt", "rb");
        if (fl) { fclose(fl); lut = 1; }
#endif
        g_bloom.fin[0].p = bloom_program(lut ? BLOOM_FINAL_FS : BLOOM_FINAL_POLY_FS, "final");
        g_bloom.fin[1].p = bloom_program(lut ? BLOOM_FINAL_DOF_FS : BLOOM_FINAL_POLY_DOF_FS,
                                         "final + depth of field");
        printf("visual treatment: %s curves\n", lut ? "LUT (vtlut.txt)" : "polynomial");
    }
    if (!g_bloom.ob || !g_bloom.blur || !g_bloom.glow || !g_bloom.fin[0].p) return 0;
    g_bloom.blur_step = glGetUniformLocation(g_bloom.blur, "uStep");
    g_bloom.glow_step = glGetUniformLocation(g_bloom.glow, "uStep");
    for (int i = 0; i < 2; i++) {
        GLuint p = g_bloom.fin[i].p;
        if (!p) continue;
        g_bloom.fin[i].vt = glGetUniformLocation(p, "uVT");
        g_bloom.fin[i].bloom = glGetUniformLocation(p, "uBloom");
        g_bloom.fin[i].lift = glGetUniformLocation(p, "uLift");
        g_bloom.fin[i].flash = glGetUniformLocation(p, "uFlash");
        g_bloom.fin[i].lbh = glGetUniformLocation(p, "uLbH");
        g_bloom.fin[i].dof = glGetUniformLocation(p, "uDof");
        g_bloom.fin[i].speed = glGetUniformLocation(p, "uSpeed");
        GLint u = glGetUniformLocation(p, "uBlur");
        if (u >= 0) glUniform1i(u, 3);   /* bloom_program left p bound */
    }
    g_bloom.speed = bloom_program_vs(SPEED_VS, SPEED_FS, "speed blur");
    g_bloom.speed_ofs = g_bloom.speed ? glGetUniformLocation(g_bloom.speed, "uOfs") : -1;
    g_bloom.dofblur = bloom_program(DOF_BLUR_FS, "depth of field blur");
    g_bloom.dofblur_step = g_bloom.dofblur ? glGetUniformLocation(g_bloom.dofblur, "uStep") : -1;
    if (!g_bloom.dofblur) g_bloom.fin[1].p = 0;
    printf("depth of field: %s\n", g_bloom.fin[1].p ? "ready" : "unavailable");
    g_bloom.obblur = bloom_program(BLOOM_OBBLUR_FS, "overbright+blur");
    g_bloom.glow2 = bloom_program(BLOOM_GLOW2_FS, "blur+glow");
#ifdef __vita__
    {   FILE *f5 = fopen("ux0:data/NFSU2/bloom5.txt", "rb");   /* A/B: the 5-pass chain */
        if (f5) { fclose(f5); g_bloom.obblur = g_bloom.glow2 = 0; } }
#endif
    {   /* ux0:data/NFSU2/bloomfused.txt: the fused 2-pass chain, for A/B */
        int fused = 0;
#ifdef __vita__
        FILE *ff = fopen("ux0:data/NFSU2/bloomfused.txt", "rb");
        if (ff) { fclose(ff); fused = 1; }
#endif
        if (!fused) {
            g_bloom.sob = bloom_program_vs(BLOOM_SEP_VS, BLOOM_SEP_OB_FS, "separable overbright+H");
            g_bloom.sblur = bloom_program_vs(BLOOM_SEP_VS, BLOOM_SEP_BLUR_FS, "separable blur");
            g_bloom.sglow = bloom_program_vs(BLOOM_SEP_VS, BLOOM_SEP_GLOW_FS, "separable V+glow");
            if (!g_bloom.sob || !g_bloom.sblur || !g_bloom.sglow) g_bloom.sob = 0;
            else {
                g_bloom.sob_step = glGetUniformLocation(g_bloom.sob, "uStep");
                g_bloom.sblur_step = glGetUniformLocation(g_bloom.sblur, "uStep");
                g_bloom.sglow_step = glGetUniformLocation(g_bloom.sglow, "uStep");
                printf("bloom: separable chain, 4 passes, non-dependent taps\n");
            }
        }
    }
    if (g_bloom.obblur && g_bloom.glow2) {
        g_bloom.obblur_step = glGetUniformLocation(g_bloom.obblur, "uStep");
        g_bloom.glow2_step = glGetUniformLocation(g_bloom.glow2, "uStep");
        printf("bloom: fused chain, 2 passes\n");
    } else {
        g_bloom.obblur = g_bloom.glow2 = 0;
        printf("bloom: 5-pass chain\n");
    }
    {   /* treatment curves (screenfilter.fx): r = gain, k 0.44 pivot 0.4
           (0x5bbdb0), g = ramp bias 0.45 */
        unsigned char lut[256 * 4];
        for (int i = 0; i < 256; i++) {
            float x = (float)i / 255.0f;
            float m = (1.0f / 0.44f - 2.0f) * (1.0f - x / 0.4f);
            float gn = x < 0.4f ? x / (m + 1.0f) : (m - x) / (m - 1.0f);
            float rb = x / ((1.0f / 0.45f - 2.0f) * (1.0f - x) + 1.0f);
            gn = gn < 0.0f ? 0.0f : gn > 1.0f ? 1.0f : gn;
            lut[i * 4 + 0] = (unsigned char)(gn * 255.0f + 0.5f);
            lut[i * 4 + 1] = (unsigned char)(rb * 255.0f + 0.5f);
            lut[i * 4 + 2] = 0; lut[i * 4 + 3] = 255;
        }
        glGenTextures(1, &g_bloom.lut);
        glBindTexture(GL_TEXTURE_2D, g_bloom.lut);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lut);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    g_bloom.state = 1;
    printf("bloom: half-res targets %dx%d\n", g_bloom.w, g_bloom.h);
    return 1;
}

static void bloom_pass(int target, GLuint prog, GLuint tex0, GpuMesh *quad) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_bloom.fbo[target]);
    glViewport(0, 0, g_bloom.w, g_bloom.h);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex0);
    draw_gpumesh(quad);
}

/* Runs the chain from g_scene.tex and leaves the composited frame in the
   default framebuffer at screen size. 0 = not available (caller blits). */
static int bloom_composite(GpuMesh *quad, int sw, int sh) {
    if (!g_bloom.state && !bloom_setup(g_scene.w, g_scene.h)) return 0;
    if (g_bloom.state != 1 || !quad) return 0;
    enum { A, B };
    int prev = 2 + g_bloom.cur, next = 2 + (g_bloom.cur ^ 1);
    float tx = 1.0f / (float)g_bloom.w, ty = 1.0f / (float)g_bloom.h;
#ifdef __vita__
    extern int vita_probe_skip;                         /* GPU probe pass 9 */
    if (vita_probe_skip == 9) { next = prev; goto composite; }
#endif
    if (!g_bloom_on) { next = prev; goto composite; }   /* G stays cleared */
    float ob_ofs, sub, add;
    bloom_levels(&ob_ofs, &sub, &add);
    if (g_bloom.sob) {
        glUseProgram(g_bloom.sob);
        glUniform2f(g_bloom.sob_step, tx, 0.0f);
        glUniform1f(glGetUniformLocation(g_bloom.sob, "uObOfs"), ob_ofs);
        bloom_pass(A, g_bloom.sob, g_scene.tex, quad);            /* overbright + H */
        glUseProgram(g_bloom.sblur);
        glUniform2f(g_bloom.sblur_step, 0.0f, ty);
        bloom_pass(B, g_bloom.sblur, g_bloom.tex[A], quad);       /* V */
        glUniform2f(g_bloom.sblur_step, tx, 0.0f);
        bloom_pass(A, g_bloom.sblur, g_bloom.tex[B], quad);       /* H */
        glUseProgram(g_bloom.sglow);
        glUniform2f(g_bloom.sglow_step, 0.0f, ty);
        glUniform1f(glGetUniformLocation(g_bloom.sglow, "uSub"), sub);
        glUniform1f(glGetUniformLocation(g_bloom.sglow, "uAdd"), add);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_scene.tex);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_bloom.tex[prev]);
        bloom_pass(next, g_bloom.sglow, g_bloom.tex[A], quad);    /* V + subtract + glow */
        g_bloom.cur ^= 1;
        goto composite;
    }
    if (g_bloom.obblur) {
        glUseProgram(g_bloom.obblur);
        glUniform2f(g_bloom.obblur_step, tx, ty);
        glUniform1f(glGetUniformLocation(g_bloom.obblur, "uObOfs"), ob_ofs);
        bloom_pass(A, g_bloom.obblur, g_scene.tex, quad);   /* overbright + H + V */
        glUseProgram(g_bloom.glow2);
        glUniform2f(g_bloom.glow2_step, tx, ty);
        glUniform1f(glGetUniformLocation(g_bloom.glow2, "uSub"), sub);
        glUniform1f(glGetUniformLocation(g_bloom.glow2, "uAdd"), add);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_scene.tex);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_bloom.tex[prev]);
        bloom_pass(next, g_bloom.glow2, g_bloom.tex[A], quad);  /* H + V + glow */
        g_bloom.cur ^= 1;
        goto composite;
    }
    glUseProgram(g_bloom.ob);
    glUniform1f(glGetUniformLocation(g_bloom.ob, "uObOfs"), ob_ofs);
    bloom_pass(A, g_bloom.ob, g_scene.tex, quad);
    glUseProgram(g_bloom.blur);
    glUniform2f(g_bloom.blur_step, tx, 0.0f);
    bloom_pass(B, g_bloom.blur, g_bloom.tex[A], quad);     /* H */
    glUniform2f(g_bloom.blur_step, 0.0f, ty);
    bloom_pass(A, g_bloom.blur, g_bloom.tex[B], quad);     /* V */
    glUniform2f(g_bloom.blur_step, tx, 0.0f);
    bloom_pass(B, g_bloom.blur, g_bloom.tex[A], quad);     /* H */
    /* last V blur + subtract the scene + update the glow buffer */
    glUseProgram(g_bloom.glow);
    glUniform2f(g_bloom.glow_step, 0.0f, ty);
    glUniform1f(glGetUniformLocation(g_bloom.glow, "uSub"), sub);
    glUniform1f(glGetUniformLocation(g_bloom.glow, "uAdd"), add);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_scene.tex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_bloom.tex[prev]);
    bloom_pass(next, g_bloom.glow, g_bloom.tex[B], quad);
    g_bloom.cur ^= 1;
composite:;  /* screen = treatment(dof(scene)) + G */
    /* depth of field (0x5cc8b0 / 0x5cc9a0): the frame box-filtered to half
       res, then twice H + V with the bloom's taps -- the tap grid does a
       box + H + V from the full-res frame, then H + V; A and B are free
       once the glow is updated */
    int dof = g_dof.frame && g_dof.on && g_bloom.fin[1].p;
    if (dof && g_bloom.sblur) {   /* the same blur as separable non-dependent passes */
        glUseProgram(g_bloom.sblur);
        glUniform2f(g_bloom.sblur_step, tx, 0.0f);
        bloom_pass(B, g_bloom.sblur, g_scene.tex, quad);          /* box + H */
        glUniform2f(g_bloom.sblur_step, 0.0f, ty);
        bloom_pass(A, g_bloom.sblur, g_bloom.tex[B], quad);       /* V */
        glUniform2f(g_bloom.sblur_step, tx, 0.0f);
        bloom_pass(B, g_bloom.sblur, g_bloom.tex[A], quad);       /* H */
        glUniform2f(g_bloom.sblur_step, 0.0f, ty);
        bloom_pass(A, g_bloom.sblur, g_bloom.tex[B], quad);       /* V */
    } else if (dof) {
        glUseProgram(g_bloom.dofblur);
        glUniform2f(g_bloom.dofblur_step, tx, ty);
        bloom_pass(B, g_bloom.dofblur, g_scene.tex, quad);
        bloom_pass(A, g_bloom.dofblur, g_bloom.tex[B], quad);
    }
    /* speed blur (0x5cc460): B = the frame's 4 taps at o, A = B's at 4o,
       blended over the frame by the strength cut to a byte (ftol, render
       state 0xc1); skipped below 1/255. Half res here (A and B are free once
       the glow is updated; not alongside the DOF, which holds A -- the NIS
       intros that use it stand still). */
    float speed = 0.0f;
    if (!dof && g_speed.on > 0 && g_speed.k >= 1.0f / 255.0f && g_bloom.speed) {
        const float *o = g_speed.o;
        glUseProgram(g_bloom.speed);
        glUniform4f(g_bloom.speed_ofs, o[0], o[1], o[2], o[3]);
        bloom_pass(B, g_bloom.speed, g_scene.tex, quad);
        glUniform4f(g_bloom.speed_ofs, 4.0f * o[0], 4.0f * o[1], 4.0f * o[2], 4.0f * o[3]);
        bloom_pass(A, g_bloom.speed, g_bloom.tex[B], quad);
        speed = (float)(int)(fminf(g_speed.k, 1.0f) * 255.0f) / 255.0f;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, sw, sh);
    {   const int f = dof;
        glUseProgram(g_bloom.fin[f].p);
        glUniform1f(g_bloom.fin[f].vt, g_vt_on ? 1.0f : 0.0f);
        glUniform1f(g_bloom.fin[f].bloom, g_bloom_on ? 1.0f : 0.0f);
        glUniform1f(g_bloom.fin[f].lift, g_vt_lift ? 0.2f : 0.0f);   /* TintColor.w */
        glUniform3f(g_bloom.fin[f].flash, g_flash[0], g_flash[1], g_flash[2]);
        glUniform1f(g_bloom.fin[f].lbh, g_dof.lbh);
        glUniform1f(g_bloom.fin[f].speed, speed);
        if (speed > 0.0f) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, g_bloom.tex[A]); }
        if (f) {
            glUniform3f(g_bloom.fin[f].dof, g_dof.F, g_dof.O1, g_dof.O2);
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, g_bloom.tex[A]);
        }
    }
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_bloom.tex[next]);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, g_bloom.lut);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_scene.tex);
    draw_gpumesh(quad);
    return 1;
}

int render_scene_begin(int screen_w, int screen_h, float scale) {
    g_scene.active = 0;
    g_dof.frame = 0;
    /* bloom reads the finished 3D frame, so it needs the scene in a texture
       even at native resolution */
    if (g_scene.failed || (scale >= 0.999f && !bloom_wanted())) return 0;
    if (scale > 1.0f) scale = 1.0f;
    int w = (int)(screen_w * scale) & ~7, h = (int)(screen_h * scale) & ~7;
    if (w < 64 || h < 64) return 0;
    if (!g_scene.fbo || g_scene.w != w || g_scene.h != h) {
        if (g_scene.fbo) { glDeleteFramebuffers(1, &g_scene.fbo); glDeleteTextures(1, &g_scene.tex);
                           glDeleteRenderbuffers(1, &g_scene.depth); g_scene.fbo = 0; }
        glGenTextures(1, &g_scene.tex);
        glBindTexture(GL_TEXTURE_2D, g_scene.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &g_scene.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_scene.fbo);
        glGenRenderbuffers(1, &g_scene.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, g_scene.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_scene.depth);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_scene.tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("scaled rendering unavailable (framebuffer incomplete); using native\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_scene.failed = 1;
            return 0;
        }
        g_scene.w = w; g_scene.h = h;
        printf("scaled rendering: 3D at %dx%d, HUD at %dx%d\n", w, h, screen_w, screen_h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_scene.fbo);
    glViewport(0, 0, g_scene.w, g_scene.h);
    g_scene.active = 1;
    return 1;
}

void render_scene_end(const RProg *rp, GpuMesh *quad, int screen_w, int screen_h,
                      float fog_density) {
    if (!g_scene.active) return;
    g_scene.active = 0;
    if (g_lb_scissor) { glDisable(GL_SCISSOR_TEST); g_lb_scissor = 0; }
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    prev_capture(quad);   /* before the DOF pass writes into the scene */
    if (g_dof.scene) render_dof_scene(0);
    int composited = bloom_wanted() && bloom_composite(quad, screen_w, screen_h);
    g_dof.frame = 0;
    if (composited) {
        glUseProgram(rp->prog);
        if (depth) glEnable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w, screen_h);
    blit_program();
    if (g_blit_prog != (GLuint)-1) {
        glUseProgram(g_blit_prog);
        glBindTexture(GL_TEXTURE_2D, g_scene.tex);
        draw_gpumesh(quad);
        glUseProgram(rp->prog);
        if (depth) glEnable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND);
        return;
    }
    /* unit quad [0,1]^2 -> NDC [-1,1]^2; texture origin is bottom-left too */
    static const float M[16] = {2,0,0,0, 0,2,0,0, 0,0,1,0, -1,-1,0,1};
    glUniformMatrix4fv(rp->uMVP, 1, GL_FALSE, M);
    render_model(rp, NULL);
    glUniform1f(rp->uFogDensity, 0.0f);
    glUniform1f(rp->uUseTex, 1.0f);
    glUniform1f(rp->uEmissiveTex, 1.0f);
    glUniform1f(rp->uVista, 0.0f);
    glUniform3f(rp->uColor, 1.0f, 1.0f, 1.0f);
    glUniform1f(rp->uAlpha, 1.0f);
    glBindTexture(GL_TEXTURE_2D, g_scene.tex);
    draw_gpumesh(quad);
    glUniform1f(rp->uEmissiveTex, 0.0f);
    glUniform1f(rp->uUseTex, 0.0f);
    glUniform1f(rp->uFogDensity, fog_density);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND);
}

/* The 3D frame's target while it renders into g_scene (bloom / scaled):
   lets a pass copy the frame on the GPU (draw g_scene.tex into its own
   texture, then rebind fbo) instead of glCopyTexSubImage2D, which vitaGL
   runs as glReadPixels + glTexSubImage2D (GPU sync + CPU copy, ~50 ms). */
int render_scene_target(unsigned *fbo, unsigned *tex, int *w, int *h) {
    if (!g_scene.active || !g_scene.fbo || !g_scene.tex) return 0;
    if (fbo) *fbo = g_scene.fbo;
    if (tex) *tex = g_scene.tex;
    if (w) *w = g_scene.w;
    if (h) *h = g_scene.h;
    return 1;
}
#endif

int upload_world_batches_step(WorldBatchUpload **slot, int max_batches,
                             N2Batch **out, int *count, int *meshbatch) {
    if (!slot || !*slot || !out || !count) return -1;
    if (max_batches <= 0) return 0;
    WorldBatchUpload *j = *slot;
    const N2Scene *s = j->scene;
    BSortEnt *ent = j->ent;
    N2Batch *bat = j->batches;
    int *run0 = j->run0, *run1 = j->run1;
    const char *audit = j->audit;
    g_batch_normals = j->normals;
    g_batch_packed = j->packed;
#ifdef __vita__
    g_batch_reuse = 1;
    if (!j->next) g_batch_reused = g_batch_reused_bytes = 0;
    {   static int copy_on = -1;
        if (copy_on < 0) { FILE *f = fopen("ux0:data/NFSU2/nogpucopy.txt", "rb"); copy_on = !f; if (f) fclose(f); }
        g_batch_copy = copy_on;
        static int one_on = -1;
        if (one_on < 0) { FILE *f = fopen("ux0:data/NFSU2/twobuf.txt", "rb"); one_on = !f; if (f) fclose(f);
                          printf("world batches: %s\n", one_on ? "one GL buffer a batch" : "vertex + index buffers"); }
        g_batch_onebuf = one_on; }
#endif
#ifdef __vita__
    /* Count limits alone allow a large resident to monopolise a frame.
       Check between complete batches; synchronous startup remains unbounded. */
    unsigned long long step_begin = sceKernelGetProcessTimeWide();
    unsigned long long budget_us = max_batches >= 128 ? 24000 : 4000;   /* smooth: a city frame has ~7 ms to spare, and a batch can run over (hwlog29: 9 ms steps at 6) */
    int sliced = max_batches != INT_MAX, emitted = 0, smooth = sliced && max_batches < 128;
#endif
    /* GL microseconds per KB uploaded, learned from this run's uploads: a
       smooth step stops before a batch that would carry the frame past its
       budget, instead of finding out after (hwlog51_L: 7-9 ms upload frames
       on a 4 ms budget, the overshoot being one large batch) */
    static double us_per_kb = 0;
    while (j->next < j->meshes && max_batches > 0) {
#ifdef __vita__
        long reused_before = g_batch_reused;
        if (smooth && emitted && us_per_kb > 0 && j->prepared && j->count < j->prepared_count) {
            const struct PreparedBatch *pb = &j->prepared[j->count];
            if (pb->meta && pb->bv && !batch_buffers_find(pb->hash, pb->nv, pb->ni)) {
                double kb = ((double)pb->nv * sizeof(BatchedVertex) + (double)pb->ni * 2) / 1024.0;
                unsigned long long spent = sceKernelGetProcessTimeWide() - step_begin;
                if ((double)spent + kb * us_per_kb > (double)budget_us) { g_batch_deferred++; break; }
            }
        }
        long bytes_before = g_batch_bytes;
        unsigned long long emit_t0 = sceKernelGetProcessTimeWide();
#endif
        int i0 = j->next, i = i0 + 1;
        if (j->prepared) {
            assert(j->count<j->prepared_count && j->prepared[j->count].begin==i0);
            i=j->prepared[j->count].end;
            g_batch_prepared_hash=j->prepared[j->count].hash;
            g_batch_prep=&j->prepared[j->count];
        } else {
        g_batch_prepared_hash=NULL; g_batch_prep=NULL;
        int verts = s->meshes[ent[i0].idx].nverts;
        while (i < j->meshes && ent[i].key == ent[i0].key &&
               ent[i].cell_x == ent[i0].cell_x && ent[i].cell_y == ent[i0].cell_y &&
               ent[i].group == ent[i0].group &&
               verts + s->meshes[ent[i].idx].nverts <= BATCH_MAXVERTS)
            verts += s->meshes[ent[i++].idx].nverts;
        }
        if (!batch_emit(s, ent, i0, i, (GLuint)ent[i0].key,
                        &bat[j->count], j->count, audit, j->modes)) {
            g_batch_normals = NULL; g_batch_packed = NULL;
            g_batch_prepared_hash=NULL; g_batch_prep=NULL;
#ifdef __vita__
            g_batch_reuse = 0;
#endif
            upload_world_batches_cancel(slot); return -1;
        }
        run0[j->count] = i0; run1[j->count] = i;
        j->count++; j->next = i;
#ifdef __vita__
        if (g_batch_bytes - bytes_before >= 16384) {
            double r = (double)(sceKernelGetProcessTimeWide() - emit_t0) * 1024.0 /
                       (double)(g_batch_bytes - bytes_before);
            us_per_kb = us_per_kb > 0 ? us_per_kb * 0.75 + r * 0.25 : r;
        }
        emitted++;
        /* Retained GPU buffers still cost CPU packing time, bounded below,
           but should not consume the allowance for actual GPU uploads. */
        if (g_batch_reused == reused_before) max_batches--;
        if (sliced && sceKernelGetProcessTimeWide() - step_begin >= budget_us) break;
#else
        max_batches--;
#endif
    }
    g_batch_normals = NULL; g_batch_packed = NULL;
    g_batch_prepared_hash=NULL; g_batch_prep=NULL;
#ifdef __vita__
    g_batch_reuse = 0;
#endif
    if (j->next < j->meshes) return 0;
#ifdef __vita__
    /* smooth: the last batches' frame leaves the sort and the mesh map
       (several ms on a city area) to the next call */
    if (smooth && emitted) return 0;
    /* the GPU-copy worker still filling this area's buffers: a sliced step
       comes back next frame, a synchronous one waits */
    if (!vita_copy_idle()) {
        if (sliced) return 0;
        unsigned long long w0 = sceKernelGetProcessTimeWide();
        vita_copy_wait();
        g_batch_wait_us += sceKernelGetProcessTimeWide() - w0;
    }
#endif
    int nb = j->count;
    if (!j->tail) {
#ifdef __vita__
    {   unsigned long long cus, cby; vita_copy_stats(&cus, &cby);
        printf("batches: %d built, CPU packing %llu ms, GL buffers %llu ms, %ld KB uploaded"
               " (%ld batches copied on the worker: %llu KB in %llu ms, frame thread waited %llu ms;"
               " their GL alloc+map %llu ms, queueing %llu ms)\n",
               nb, g_batch_pack_us / 1000, g_batch_gl_us / 1000, g_batch_bytes >> 10,
               g_batch_copied, cby >> 10, cus / 1000, g_batch_wait_us / 1000,
               g_batch_alloc_us / 1000, g_batch_push_us / 1000); }
    g_batch_alloc_us = g_batch_push_us = 0;
    g_batch_pack_us = g_batch_gl_us = 0; g_batch_bytes = 0; g_batch_copied = 0; g_batch_wait_us = 0;
    printf("tile GPU reuse: %ld batches, %ld KB retained without upload; "
           "%ld meshes (%ld KB) rebuilt from source (CPU copy freed); %ld uploaded from worker-assembled arrays, %ld steps ended early\n",
           g_batch_reused, g_batch_reused_bytes >> 10, g_batch_src_meshes,
           g_batch_src_verts * (long)sizeof(BatchedVertex) >> 10, g_batch_asm, g_batch_deferred);
    g_batch_reused = g_batch_reused_bytes = 0; g_batch_src_meshes = g_batch_src_verts = 0;
    g_batch_asm = 0; g_batch_deferred = 0;
    hitch_mark("fin.up_stats");
#endif
    /* minimise texture binds: texture order, emission order within a
       texture. hwlog72/73: qsort was 5-9 ms a build on the Vita (moving the
       ~120-byte batches, then even 8-byte keys: newlib's compare calls); a
       stable LSD radix sort of the indices by texture name (8-bit digits,
       a pass skipped when every key shares that digit), then one copy. */
    {   unsigned *ix = nb > 1 ? malloc((size_t)nb * 2 * sizeof *ix) : NULL;
        N2Batch *sorted = ix ? malloc((size_t)nb * sizeof *sorted) : NULL;
        if (sorted) {
            unsigned *a = ix, *t = ix + nb, orv = 0, andv = ~0u;
            for (int b = 0; b < nb; b++) { a[b] = (unsigned)b; orv |= bat[b].tex; andv &= bat[b].tex; }
            for (int sh = 0; sh < 32; sh += 8) {
                if ((((orv ^ andv) >> sh) & 0xffu) == 0) continue;   /* same digit everywhere */
                int cnt[257]; memset(cnt, 0, sizeof cnt);
                for (int b = 0; b < nb; b++) cnt[((bat[a[b]].tex >> sh) & 0xffu) + 1]++;
                for (int d = 0; d < 256; d++) cnt[d + 1] += cnt[d];
                for (int b = 0; b < nb; b++) t[cnt[(bat[a[b]].tex >> sh) & 0xffu]++] = a[b];
                unsigned *sw = a; a = t; t = sw;
            }
#ifdef __vita__
            hitch_mark("fin.up_keys");
#endif
#ifdef OPENUG2_SIM_PRE_H
            {   static int bad_said;   /* test aid (simulator): texture order, emission order within one */
                for (int b = 1; b < nb && !bad_said; b++)
                    if (bat[a[b-1]].tex > bat[a[b]].tex || (bat[a[b-1]].tex == bat[a[b]].tex && a[b-1] > a[b])) {
                        bad_said = 1; printf("batches: radix order WRONG at %d%c", b, 10); }
                static int ok_said; if (!ok_said) { ok_said = 1; printf("batches: radix order checked (%d)%c", nb, 10); } }
#endif
            for (int b = 0; b < nb; b++) sorted[b] = bat[a[b]];
            free(bat); bat = j->batches = sorted;   /* the smooth tail reads it back next call */
        } else if (nb > 1) qsort(bat, (size_t)nb, sizeof *bat, btex_cmp);
        free(ix); }
#ifdef __vita__
    hitch_mark("fin.up_sort");
    /* hwlog69: the completing step was 10-13 ms on every downtown build
       (fin.batch_upload) against a 4 ms slice -- the tail after the last
       batch; a smooth finish leaves the mesh map and the frees to the
       next frame */
    if (smooth) { j->tail = 1; return 0; }
#endif
    }
    /* mesh -> final batch, replayed from each batch's own emission run so it is
       the production partition rather than a second derivation (M133) */
    if (meshbatch) for (int k = 0; k < s->count; k++) meshbatch[k] = -1;
    if (meshbatch)
        for (int b = 0; b < nb; b++) {
            int e = bat[b].emit_idx;
            for (int k = run0[e]; k < run1[e]; k++) meshbatch[ent[k].idx] = b;
        }
#ifdef __vita__
    hitch_mark("fin.up_meshmap");
#endif
    /* "#N" audits the batch the RENDERER calls N, i.e. wbatch[N] after this
       sort. Membership is replayed from that batch's own recorded emission run,
       so it is the production partition, not a second derivation. */
    if (audit && audit[0] == '#') {
        int want = atoi(audit + 1);
        if (want >= 0 && want < nb) {
            int e = bat[want].emit_idx;
            batch_members_report(s, ent, run0[e], run1[e], want, bat[want].tex, &bat[want]);
        } else fprintf(stderr, "batch audit: #%d out of range (0..%d)\n", want, nb-1);
    }
    *out = bat; *count = nb;
    j->batches = NULL; j->count = 0;
    upload_world_batches_cancel(slot);
#ifdef __vita__
    hitch_mark("fin.up_free");
#endif
    return 1;
}

int upload_world_batches(const N2Scene *s, const float (*mbb)[4],
                         const GLuint *mtex, GLuint texTerr, N2Batch **out,
                         const char *audit, int *meshbatch,
                         const unsigned char *mtexmode) {
    WorldBatchUpload *j = upload_world_batches_begin(s, mbb, mtex, texTerr,
                                                    audit, mtexmode);
    int count = 0;
    if (!j || upload_world_batches_step(&j, INT_MAX, out, &count, meshbatch) < 0) {
        upload_world_batches_cancel(&j); return -1;
    }
    return count;
}

/* Same merge as above but for exactly one category, grouped by texture only
 * (no spatial grid — a city has a handful of skybox/neon meshes, not tens of
 * thousands, so there's nothing for a cell split to buy here). */
int upload_cat_batches(const N2Scene *s, int cat, const GLuint *mtex, N2Batch **out,
                       const unsigned char *mtexmode) {
    int n = s->count;
    BSortEnt *ent = (BSortEnt *)malloc((size_t)(n ? n : 1) * sizeof *ent);
    if (!ent) return -1;
    int m = 0;
    for (int i = 0; i < n; i++)
        if (s->meshes[i].cat == cat) {
            ent[m].key = mtex[i]; ent[m].idx = i;
            ent[m].cell_x = ent[m].cell_y = 0;
            int mode = mtexmode ? mtexmode[i] : N2_DRAW_OPAQUE;
            ent[m].group = (unsigned char)n2_world_batch_material_group(
                &s->meshes[i], mode);
            m++;
        }
    qsort(ent, (size_t)m, sizeof *ent, bsort_cmp);
    int cap = 8, nb = 0;
    N2Batch *bat = (N2Batch *)malloc((size_t)cap * sizeof *bat);
    if (!bat) { free(ent); return -1; }
    int i0 = 0, verts = 0;
    for (int i = 0; i <= m; i++) {
        int flush = (i == m) || (i > i0 && (ent[i].key != ent[i0].key ||
                                            ent[i].group != ent[i0].group)) ||
                    (i > i0 && verts + s->meshes[ent[i].idx].nverts > BATCH_MAXVERTS);
        if (flush && i > i0) {
            if (nb == cap) {
                cap *= 2;
                N2Batch *grown = realloc(bat, (size_t)cap * sizeof *bat);
                if (!grown) goto fail;
                bat = grown;
            }
            if (!batch_emit(s, ent, i0, i, (GLuint)ent[i0].key,
                            &bat[nb], nb, NULL, mtexmode)) goto fail;
            nb++;
            i0 = i; verts = 0;
        }
        if (i < m) verts += s->meshes[ent[i].idx].nverts;
    }
    free(ent);
    *out = bat;
    return nb;
fail:
    free(ent); render_batch_array_free(&bat, &nb); return -1;
}

void render_batch_array_free(N2Batch **batches, int *count) {
    if (!batches) return;
    if (*batches) {
        int n = count && *count > 0 ? *count : 0;
        for (int i = 0; i < n; i++) {
            render_batch_release(&(*batches)[i]);
        }
        free(*batches);
    }
    *batches = NULL;
    if (count) *count = 0;
}

void render_batch_reuse_selftest(void) {
#ifdef __vita__
    /* Isolate upload checks from errors left by graphics initialization. */
    GLenum prior_error;
    while ((prior_error = glGetError()) != GL_NO_ERROR)
        printf("tile GPU reuse selftest: prior GL error 0x%x\n", (unsigned)prior_error);
    float vertices[5][15] = {{-4,-4,0,0,0, -2,-4,0,1,0, -4,-2,0,0,1},
        {252,252,0,0,0, 254,252,0,1,0, 252,254,0,0,1},
        {1024,1024,0,0,0, 1026,1024,0,1,0, 1024,1026,0,0,1},
        {2048,2048,0,0,0, 2050,2048,0,1,0, 2048,2050,0,0,1},
        {3072,3072,0,0,0, 3074,3072,0,1,0, 3072,3074,0,0,1}};
    uint16_t indices[] = {0,1,2}; N2Mesh meshes[5] = {{0}};
    float bounds[5][4]; GLuint textures[5] = {0};
    for (int i = 0; i < 5; i++) {
        meshes[i].verts = vertices[i]; meshes[i].idx = indices;
        meshes[i].nverts = meshes[i].nidx = 3; meshes[i].cat = N2_OTHER;
        bounds[i][0] = bounds[i][1] = vertices[i][0];
        bounds[i][2] = bounds[i][3] = vertices[i][0] + 2;
    }
    N2Scene first = {meshes, 2, 2}, second = {meshes, 3, 3};
    /* Packed scenery survives resident retirement and is borrowed unchanged
       by the next worker, rather than recomputing its normals and colours. */
    N2Mesh cached = meshes[0];
    cached.verts = malloc(sizeof vertices[0]); cached.idx = malloc(sizeof indices);
    cached.shared_geometry = calloc(1,sizeof(N2SharedGeometry));
    assert(cached.verts && cached.idx && cached.shared_geometry);
    memcpy(cached.verts,vertices[0],sizeof vertices[0]);
    memcpy(cached.idx,indices,sizeof indices);
    cached.shared_geometry->refs=2;
    cached.shared_geometry->verts=cached.verts; cached.shared_geometry->idx=cached.idx;
    N2Mesh retained_mesh=cached;
    PackedMesh packed_a={0},packed_b={0};
    assert(sizeof(BatchedVertex)==28);   /* Vita layout (render.h) */
    g_pack_keep_selftest = 1;
    assert(render_pack_mesh(&cached,&packed_a));
    assert(render_pack_mesh(&retained_mesh,&packed_b));
    g_pack_keep_selftest = 0;
    assert(packed_a.borrowed && packed_b.borrowed && packed_a.v==packed_b.v);
    n2_mesh_geometry_release(&cached);
    /* P1b (11-15): the packed copy is dropped once hashed (v NULL) unless
       keeppacked.txt keeps it */
    assert(retained_mesh.shared_geometry->refs==1 && (!packed_b.v || packed_b.v[0].pos[0]==-4));
    n2_mesh_geometry_release(&retained_mesh);
    N2Batch *a = NULL, *b = NULL; int na, nb;
    na = upload_world_batches(&first,bounds,textures,0,&a,NULL,NULL,NULL);
    nb = upload_world_batches(&second,bounds,textures,0,&b,NULL,NULL,NULL);
    printf("tile GPU reuse selftest: upload counts %d, %d\n", na, nb);
    assert(na == 2 && nb == 3);
    int shared = 0;
    for (int i = 0; i < na; i++) for (int j = 0; j < nb; j++)
        if (a[i].vbo == b[j].vbo && a[i].ibo == b[j].ibo) shared++;
    assert(shared == 2); /* distant added tile must not change existing runs */
    /* Reused batches pass the one-upload allowance; the fourth new batch
       stops the slice with the fifth pending. Cancellation releases both
       borrowed references and the newly allocated GPU buffers. */
    N2Scene five = {meshes,5,5};
    WorldBatchUpload *job = upload_world_batches_begin(&five,bounds,textures,0,NULL,NULL);
    N2Batch *partial = NULL; int partial_count = 0;
    assert(job && !upload_world_batches_step(&job,1,&partial,&partial_count,NULL));
    upload_world_batches_cancel(&job); /* releases partial shared references */
    render_batch_array_free(&a,&na);
    for (int j = 0; j < nb; j++) {
        SharedBatchBuffers *p = b[j].shared_buffers;
        assert(p && p->refs == 1);
        GLint size = 0; glBindBuffer(GL_ARRAY_BUFFER,b[j].vbo);
        glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
        assert(size == 3 * sizeof(BatchedVertex));
    }
    /* Same placement with changed UV must allocate different GPU contents. */
    vertices[0][3] = 0.25f;
    N2Batch *changed = NULL; int nc = upload_world_batches(&first,bounds,textures,0,&changed,NULL,NULL,NULL);
    assert(nc == 2);
    int retained = 0;
    for (int i = 0; i < nc; i++) for (int j = 0; j < nb; j++)
        if (changed[i].vbo == b[j].vbo) retained++;
    assert(retained == 1);
    PackedMesh plan_packed[2]={{0}}; unsigned char plan_modes[2]={0};
    assert(render_pack_mesh(&meshes[0],&plan_packed[0]));
    assert(render_pack_mesh(&meshes[1],&plan_packed[1]));
    WorldBatchUpload *plan=upload_world_batches_prepare_cpu(&first,bounds,textures,plan_modes,plan_packed);
    assert(plan && upload_world_batches_bind_prepared(plan,textures,0,plan_modes));
    N2Batch *planned=NULL; int planned_count=0;
    assert(upload_world_batches_step(&plan,INT_MAX,&planned,&planned_count,NULL)==1);
    assert(planned_count==nc);
    int matched=0;
    for (int i=0; i<planned_count; i++) for (int k=0; k<nc; k++)
        if (planned[i].vbo==changed[k].vbo && planned[i].ibo==changed[k].ibo) matched++;
    assert(matched==nc); /* prepared and fallback fingerprints must agree */
    assert(!memcmp(planned,changed,(size_t)nc*sizeof *planned));
    render_batch_array_free(&planned,&planned_count);
    unsigned char mismatch_modes[2]={N2_DRAW_CUTOUT,0};
    plan=upload_world_batches_prepare_cpu(&first,bounds,textures,plan_modes,plan_packed);
    assert(plan && !upload_world_batches_bind_prepared(plan,textures,0,mismatch_modes));
    upload_world_batches_cancel(&plan);
    float merged_bounds[2][4];
    memcpy(merged_bounds[0],bounds[0],sizeof merged_bounds[0]);
    memcpy(merged_bounds[1],bounds[0],sizeof merged_bounds[1]);
    GLuint mismatch_textures[2]={0,1};
    plan=upload_world_batches_prepare_cpu(&first,merged_bounds,textures,plan_modes,plan_packed);
    assert(plan && !upload_world_batches_bind_prepared(plan,mismatch_textures,0,plan_modes));
    upload_world_batches_cancel(&plan);
    assert(!plan_packed[0].v && !plan_packed[1].v);
    render_batch_array_free(&changed,&nc);
    render_batch_array_free(&b,&nb);
    for (int i = 0; i < SHARED_BATCH_SLOTS; i++) assert(!g_shared_batches[i]);
    assert(glGetError() == GL_NO_ERROR);
    g_batch_reused = g_batch_reused_bytes = 0;
#endif
}

void draw_batch(const N2Batch *b) {
#ifdef OPENUG2_SIM_PRE_H
    { extern uint32_t g_sim_probe_tag; g_sim_probe_tag = b->texkey; }   /* test aid: "probe" names the batch's texture */
#endif
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BatchedVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchedVertex), (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, BV_NORMAL_GL_TYPE, BV_NORMAL_GL_NORM, sizeof(BatchedVertex),
                          (void*)offsetof(BatchedVertex, normal));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BatchedVertex),
                          (void*)offsetof(BatchedVertex, col));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
    glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT, (const void *)(uintptr_t)b->ioff);
}

/* Build a clean procedural wheel (short cylinder, axle along Y, disc in X-Z) —
 * the game's rim meshes are sparse spoke shells that read as spiky urchins when
 * drawn solid, so we substitute a simple tyre. Modelled at the origin like the
 * game wheel, so the same wheelT placement transforms apply. */
GpuMesh make_wheel(float R, float halfW) {
    const int N = 24;
    int nv = N*2 + 2, nt = N*4;         /* 2 rings + 2 hub centres; sides + 2 caps */
    N2Mesh m; memset(&m, 0, sizeof m);
    m.nverts = nv; m.verts = (float *)malloc(nv*5*sizeof(float));
    for (int i = 0; i < N; i++) {
        float a = 2.0f*(float)M_PI*i/N, cx = R*cosf(a), cz = R*sinf(a);
        float u = cosf(a)*0.5f+0.5f, v = sinf(a)*0.5f+0.5f;
        float *p0 = m.verts + i*5;     p0[0]=cx; p0[1]= halfW; p0[2]=cz; p0[3]=u; p0[4]=v;
        float *p1 = m.verts + (N+i)*5; p1[0]=cx; p1[1]=-halfW; p1[2]=cz; p1[3]=u; p1[4]=v;
    }
    int c0 = 2*N, c1 = 2*N+1;
    float *h0=m.verts+c0*5; h0[0]=0;h0[1]= halfW;h0[2]=0;h0[3]=0.5f;h0[4]=0.5f;
    float *h1=m.verts+c1*5; h1[0]=0;h1[1]=-halfW;h1[2]=0;h1[3]=0.5f;h1[4]=0.5f;
    m.idx = (uint16_t *)malloc(nt*3*sizeof(uint16_t)); int k=0;
    for (int i = 0; i < N; i++) {
        int j = (i+1)%N;
        m.idx[k++]=i;    m.idx[k++]=j;    m.idx[k++]=N+j;      /* tread quad */
        m.idx[k++]=i;    m.idx[k++]=N+j;  m.idx[k++]=N+i;
        m.idx[k++]=c0;   m.idx[k++]=j;    m.idx[k++]=i;         /* +Y face */
        m.idx[k++]=c1;   m.idx[k++]=N+i;  m.idx[k++]=N+j;       /* -Y face */
    }
    m.nidx = k; m.cat = N2_CAR_TIRE;
    N2Scene s; s.meshes = &m; s.count = 1; s.cap = 1;
    GpuMesh *g = upload_scene(&s); GpuMesh out = *g;
    free(g); free(m.verts); free(m.idx);
    return out;
}

/* Procedural alloy-rim texture for the wheel caps: the cap UVs are radial
 * (centre 0.5,0.5, ring at uv-radius 0.5), so paint by relative radius —
 * bright hub disc, spoked metal mid, dark rubber edge. The tread quads
 * sample the outer ring = rubber. */
GLuint make_wheel_tex(void) {
    enum { S = 64 };
    static unsigned char px[S*S*3];
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float dx = (x+0.5f)/S - 0.5f, dy = (y+0.5f)/S - 0.5f;
        float r = sqrtf(dx*dx + dy*dy) * 2.0f;      /* 0 centre .. 1 ring */
        unsigned char v;
        if (r > 0.78f)      v = 14;                                  /* rubber */
        else if (r > 0.30f) {                                        /* spokes */
            float spoke = 0.5f + 0.5f*cosf(5.0f*atan2f(dy, dx));
            v = (unsigned char)(38 + 70.0f*spoke*spoke);
        } else               v = r < 0.10f ? 150 : 105;              /* hub */
        unsigned char *o = px + (y*S + x)*3;
        o[0] = v; o[1] = v; o[2] = (unsigned char)(v + v/16);        /* cool metal */
    }
    N2Tex t = { S, S, px, NULL, NULL, 0, 0, 0, 0,0,0,0 };
    GLuint id = upload_tex(&t);
    /* radial, single-sample cap texture (no tiling intended) — clamp so a
       filter footprint near u/v=0 or 1 can't wrap and bleed in colour from
       the opposite edge, same reasoning as the car atlas/vinyl clamps. */
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

/* unit-quad buffers for the 2D HUD / billboards (drawn in NDC via uMVP) */
GpuMesh make_quad(void) {
    GpuMesh quad; memset(&quad, 0, sizeof quad);
    float qv[] = {0,0,0,0,0, 1,0,0,1,0, 1,1,0,1,1, 0,1,0,0,1};
    float qn[] = {0,0,1, 0,0,1, 0,0,1, 0,0,1};
    uint16_t qi[] = {0,1,2, 0,2,3};
    glGenBuffers(1,&quad.vbo); glBindBuffer(GL_ARRAY_BUFFER,quad.vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof qv,qv,GL_STATIC_DRAW);
    glGenBuffers(1,&quad.nbo); glBindBuffer(GL_ARRAY_BUFFER,quad.nbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof qn,qn,GL_STATIC_DRAW);
    glGenBuffers(1,&quad.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,quad.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof qi,qi,GL_STATIC_DRAW);
    quad.nidx = 6;
    return quad;
}

void draw_gpumesh(GpuMesh *g) {
    if (g->cbo) {
        glBindBuffer(GL_ARRAY_BUFFER,g->cbo);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,(void*)0);
    } else glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(3*sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, g->nbo);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
    glDrawElements(GL_TRIANGLES, g->nidx, GL_UNSIGNED_SHORT, 0);
}

/* ---- car.fx state (see carfx.c) ---- */
static struct {
    int checked, enabled, active;
    GLint fx, sh[10], dmin, drange, smin, srange, pw, emin, erange;
    const CarFxMat *last;
} g_cfx;
int render_carfx_enabled(void) {
    if (!g_cfx.checked) {
        g_cfx.checked = 1;
        FILE *f = fopen("ux0:data/NFSU2/oldcar.txt", "r");
        g_cfx.enabled = !f; if (f) fclose(f);
        printf("car shading: %s\n", g_cfx.enabled ? "original car.fx" : "port shader (oldcar.txt)");
    }
    return g_cfx.enabled;
}
void render_carfx_begin(const RProg *r) {
    if (!render_carfx_enabled() || !carfx_loaded()) return;
    if (!g_cfx.fx) {
        static const char *const n[10] = { "uSH0","uSH1","uSH2","uSH3","uSH4","uSH5","uSH6","uSH7","uSH8","uSH9" };
        g_cfx.fx = glGetUniformLocation(r->prog, "uCarFx");
        for (int i = 0; i < 10; i++) g_cfx.sh[i] = glGetUniformLocation(r->prog, n[i]);
        g_cfx.dmin = glGetUniformLocation(r->prog, "uCDiffMin");
        g_cfx.drange = glGetUniformLocation(r->prog, "uCDiffRange");
        g_cfx.smin = glGetUniformLocation(r->prog, "uCSpecMin");
        g_cfx.srange = glGetUniformLocation(r->prog, "uCSpecRange");
        g_cfx.pw = glGetUniformLocation(r->prog, "uCPow");
        g_cfx.emin = glGetUniformLocation(r->prog, "uCEnvMin");
        g_cfx.erange = glGetUniformLocation(r->prog, "uCEnvRange");
        int ok = g_cfx.fx != -1 && g_cfx.sh[9] != -1 && g_cfx.erange != -1 && g_cfx.dmin != -1;
        printf("car shading: uniforms %s\n", ok ? "ok" : "MISSING (main program dropped them)");
        if (!ok) g_cfx.fx = -1;
    }
    if (g_cfx.fx == -1) return;
    glActiveTexture(GL_TEXTURE0 + RENDER_ENV_UNIT);
    glBindTexture(GL_TEXTURE_2D, render_env_texture());
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(g_cfx.fx, 1.0f);
    g_cfx.active = 1; g_cfx.last = NULL;
}
void render_carfx_sh(const float sh[10][4]) {
    if (!g_cfx.active) return;
    for (int i = 0; i < 10; i++) glUniform4fv(g_cfx.sh[i], 1, sh[i]);
}
static uint32_t g_cfx_swap[8]; static int g_cfx_nswap;
void render_carfx_swap(const uint32_t *materials, int n) {
    g_cfx_nswap = n < 0 ? 0 : n > 8 ? 8 : n;
    for (int i = 0; i < g_cfx_nswap; i++) g_cfx_swap[i] = materials[i];
}
int render_carfx_swapped(uint32_t material) {
    for (int i = 0; material && i < g_cfx_nswap; i++) if (g_cfx_swap[i] == material) return 1;
    return 0;
}
void render_carfx_part(uint32_t material, int cat) {
    if (!g_cfx.active) return;
    int swapped = render_carfx_swapped(material);
    const CarFxMat *m = material && !swapped ? carfx_material(material) : NULL;
    if (!m) m = swapped || cat == N2_CAR_BODY || cat == N2_CAR_MISC ? carfx_paint() : carfx_default();
    if (m == g_cfx.last) return;
    g_cfx.last = m;
    glUniform4fv(g_cfx.dmin, 1, m->diff_min);
    glUniform4fv(g_cfx.drange, 1, m->diff_range);
    glUniform3fv(g_cfx.smin, 1, m->spec_min);
    glUniform3fv(g_cfx.srange, 1, m->spec_range);
    glUniform3f(g_cfx.pw, m->spec_pow, m->env_pow, 0.0f);
    glUniform3fv(g_cfx.emin, 1, m->env_min);
    glUniform3fv(g_cfx.erange, 1, m->env_range);
}
int render_carfx_active(void) { return g_cfx.active; }

/* worldbone.fx vs_1_1 (decoded 10-11): v0 pos, v1 normal, v2 uv, v3 weights,
   v4 indices into BlendMatrices (16 per slice); the normal goes through the
   first bone only. r = SH(N) with the car.fx quadratic form, E = the eye
   direction (unclamped dot), oD0.rgb = (DiffuseMin + DiffuseRange * N.E) *
   r, oD0.a = DiffuseMin.a + DiffuseRange.a * N.E; ps_1_1: 2 * tex * oD0
   (with depth of field on, its second pass writes the depth alpha: uDofW). No fog
   term in the effect. The skinning itself runs on the CPU (nisdraw.c). */
static const char *VS_BONE =
    GLSL_VS_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec3 aNor;\n"
    "uniform mat4 uMVP; uniform vec3 uEye;\n"
    "uniform vec4 uSH0, uSH1, uSH2, uSH3, uSH4, uSH5, uSH6, uSH7, uSH8, uSH9;\n"
    "uniform vec4 uDMin, uDRange;\n"
    "varying vec2 vUV; varying vec4 vD; varying float vDepth;\n"
    "void main(){\n"
    "  vec3 n=aNor;\n"
    "  vec4 s=uSH0+uSH1*(n.x*n.x)+uSH2*(n.y*n.y)+uSH3*(n.z*n.z)+uSH4*(n.y*n.x)\n"
    "        +uSH5*(n.x*n.z)+uSH6*(n.z*n.y)+uSH7*n.x+uSH8*n.y+uSH9*n.z;\n"
    "  float d=dot(normalize(uEye-aPos),n);\n"
    "  vec4 dl=uDMin+uDRange*d;\n"
    "  vD=clamp(vec4(dl.rgb*s.rgb,dl.a),0.0,1.0);\n"
    "  vUV=aUV; gl_Position=uMVP*vec4(aPos,1.0); vDepth=gl_Position.w; }\n";
static const char *FS_BONE =
    GLSL_HEADER
    "varying vec2 vUV; varying vec4 vD; varying float vDepth;\n"
    "uniform sampler2D uTex; uniform float uCut, uBlend, uDofW;\n"
    "void main(){\n"
    "  vec4 t=texture2D(uTex,vUV);\n"
    "  if(uCut>0.5 && t.a<0.5) discard;\n"
    "  gl_FragColor=vec4(clamp(2.0*t.rgb*vD.rgb,0.0,1.0),uBlend>0.5?t.a:" DOF_ALPHA("1.0") "); }\n";
GLuint render_bone_program(void) {
    static GLuint prog; static int tried;
    if (tried) return prog;
    tried = 1;
#ifdef __vita__
    prog = program_cached(VS_BONE, FS_BONE);
#else
    prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, VS_BONE));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, FS_BONE));
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glBindAttribLocation(prog, 2, "aNor");
    glLinkProgram(prog);
#endif
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    printf("worldbone program: %s\n", ok ? "ok" : "link failed");
    if (!ok) { glDeleteProgram(prog); prog = 0; }
    return prog;
}
/* worldnodepth.fx (SPEED2.EXE, the Acid particles, aciddraw.c): colour =
   saturate(2 * tex * vertex colour), alpha = tex * vertex alpha, no fog. */
static const char *VS_PARTICLE =
    GLSL_VS_HEADER
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec4 aColor;\n"
    "uniform mat4 uMVP;\n"
    "varying vec2 vUV; varying vec4 vC;\n"
    "void main(){ vUV=aUV; vC=aColor; gl_Position=uMVP*vec4(aPos,1.0); }\n";
static const char *FS_PARTICLE =
    GLSL_HEADER
    "varying vec2 vUV; varying vec4 vC;\n"
    "uniform sampler2D uTex;\n"
    "void main(){ vec4 t=texture2D(uTex,vUV);\n"
    "  gl_FragColor=vec4(clamp(2.0*t.rgb*vC.rgb,0.0,1.0),t.a*vC.a); }\n";
GLuint render_particle_program(void) {
    static GLuint prog; static int tried;
    if (tried) return prog;
    tried = 1;
#ifdef __vita__
    /* Contiguous locations (colour at 2, RENDER_PARTICLE_COLOR_LOC): with
       0, 1, 3 vitaGL takes its unaligned-attribute path, and the particles
       drew as solid saturated squares (vertex colour no longer 0..1). */
    prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, VS_PARTICLE));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, FS_PARTICLE));
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glBindAttribLocation(prog, RENDER_PARTICLE_COLOR_LOC, "aColor");
    glLinkProgram(prog);
#else
    prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, VS_PARTICLE));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, FS_PARTICLE));
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glBindAttribLocation(prog, RENDER_PARTICLE_COLOR_LOC, "aColor");
    glLinkProgram(prog);
#endif
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    printf("worldnodepth program: %s\n", ok ? "ok" : "link failed");
    if (!ok) { glDeleteProgram(prog); prog = 0; }
    return prog;
}
void render_carfx_end(void) {
    carfx_use_paint(0);   /* a car's own paint material ends with its pass */
    if (!g_cfx.active) return;
    glUniform1f(g_cfx.fx, 0.0f);
    g_cfx.active = 0;
}

void render_wheel_mesh(const RProg *r, GpuMesh *mesh, GLuint texture, int mode) {
    render_wheel_mesh_n(r, mesh, texture, mode, 0, NULL, NULL, -1);
}
/* n > 0: the same mesh drawn n times (the 4 wheels), each with its own MVP
   (uniform uMVP) and model matrix, under one state setup and restore */
void render_wheel_mesh_n(const RProg *r, GpuMesh *mesh, GLuint texture, int mode,
                         int n, const float (*mvp)[16], const float (*world)[16], GLint uMVP) {
    const GLint loc[]={r->uUseTex,r->uAlphaTest,r->uTextureAlpha,r->uAlpha,r->uDecal,
                       r->uRimTint,r->uSpec,r->uEnv,r->uClearcoat};
    float saved[9];for(int i=0;i<9;i++)glGetUniformfv(r->prog,loc[i],saved+i);
    GLint oldtex,src,dst,srca,dsta;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldtex);
    glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srca);glGetIntegerv(GL_BLEND_DST_ALPHA,&dsta);
    GLboolean blend=glIsEnabled(GL_BLEND),depth=glIsEnabled(GL_DEPTH_TEST),mask;
    glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
    unsigned char amode = mesh->draw_mode ? mesh->draw_mode : (unsigned char)mode;
    int cut=texture && amode==N2_DRAW_CUTOUT, translucent=texture && amode==N2_DRAW_BLEND;
    /* The wheel INTERIOR slice is the inboard barrel/backing. Its atlas
       rectangle also contains transparent texels from the tread strip, so
       alpha discard breaks the generated round backing into black facets.
       Keep its authored RGB (usually black) but make the backing continuous;
       TIRE and RIM retain their authored cutout textures. */
    if (mesh->car_material == N2_MAT_INTERIOR) cut = 0;
    glBindTexture(GL_TEXTURE_2D,texture);
    glUniform1f(r->uUseTex,texture?1.0f:0.0f);
    glUniform1f(r->uAlphaTest,cut?1.0f:0.0f);
    glUniform1f(r->uTextureAlpha,translucent?1.0f:0.0f);
    glUniform1f(r->uAlpha,1.0f);glUniform1f(r->uDecal,0.0f);
    uint32_t material=mesh->car_material;
    if (!texture || (material!=N2_MAT_MAGSILVER && material!=N2_MAT_MAGCHROME &&
                     !render_carfx_swapped(material)))
        glUniform1f(r->uRimTint,0.0f);
    if (material==N2_MAT_RUBBER || material==N2_MAT_INTERIOR || material==N2_MAT_DULLPLASTIC) {
        glUniform1f(r->uSpec,0.0f);glUniform1f(r->uEnv,0.0f);
        glUniform1f(r->uClearcoat,0.0f);
    }
    glEnable(GL_DEPTH_TEST);glDepthMask(translucent?GL_FALSE:GL_TRUE);
    if(translucent){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    else glDisable(GL_BLEND);
    render_carfx_part(mesh->car_material, mesh->cat);
    if(n<=0)draw_gpumesh(mesh);
    else for(int w=0;w<n;w++){
        glUniformMatrix4fv(uMVP,1,GL_FALSE,mvp[w]);render_model(r,world[w]);
        draw_gpumesh(mesh);
    }
    for(int i=0;i<9;i++)glUniform1f(loc[i],saved[i]);
    glBindTexture(GL_TEXTURE_2D,(GLuint)oldtex);
    glBlendFuncSeparate((GLenum)src,(GLenum)dst,(GLenum)srca,(GLenum)dsta);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    glDepthMask(mask);
}

/* A split material slice shares its vertex pool with other surfaces. Only
 * indexed vertices describe the centre that should determine its draw depth. */
static void mesh_indexed_center(const N2Mesh *m,float center[3]) {
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
    for(int j=0;j<m->nidx;j++)for(int a=0;a<3;a++) {
        float v=m->verts[m->idx[j]*5+a];
        if(v<lo[a])lo[a]=v;if(v>hi[a])hi[a]=v;
    }
    for(int a=0;a<3;a++)center[a]=(lo[a]+hi[a])*.5f;
}

void render_wheel_order(const N2Scene *scene, const float mvp[4][16], int *order) {
    if(!scene || scene->count<=0 || !order)return;
    int n=scene->count;float depth[4*n];
    for(int i=0;i<n;i++) {
        float center[3];mesh_indexed_center(scene->meshes+i,center);
        for(int k=0;k<4;k++) {
            int at=k*n+i;order[at]=at;depth[at]=mvp[k][15];
            for(int a=0;a<3;a++)depth[at]+=mvp[k][a*4+3]*center[a];
        }
    }
    /* ponytail: source-slice centres give painter order, not per-triangle
       transparency. Intersecting translucent surfaces still need finer sorting. */
    n2_sort_back_to_front(order,4*n,depth);
}

int render_car_glass_order(const N2Scene *scene,const float mvp[16],int *order) {
    if(!scene || scene->count<=0 || !order)return 0;
    float depth[scene->count];int n=0;
    for(int i=0;i<scene->count;i++) {
        const N2Mesh *m=scene->meshes+i;
        if(m->car_mount!=N2_MOUNT_BODY || (m->cat!=N2_CAR_GLASS &&
           !n2_lamp_clear_cover(m->cat,m->car_material)))continue;
        float center[3];mesh_indexed_center(m,center);
        order[n++]=i;depth[i]=mvp[15];
        for(int a=0;a<3;a++)depth[i]+=mvp[a*4+3]*center[a];
    }
    /* ponytail: pane centres, as for wheels; intersecting panes need finer sorting. */
    n2_sort_back_to_front(order,n,depth);return n;
}

/* S3TC format enums + capability flag. glext.h / SDL_opengl.h define these on
   desktop; guard so the -DN2_GLES build (where the flag stays 0 unless the GPU
   advertises the extension) still compiles. */
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
int g_tex_s3tc = 0;
int g_world_dxt = 1;
int render_world_dxt_enabled(void) { return g_world_dxt && g_tex_s3tc; }

int render_dxt_base_plan(const N2Tex *t, int *w, int *h, int *offset, int *bytes) {
    if (!t || t->prehalved || !t->dxt || (t->dxtfmt != 1 && t->dxtfmt != 3 && t->dxtfmt != 5) ||
        t->w < 4 || t->h < 4 || t->w > 2048 || t->h > 2048 ||
        (t->w & (t->w-1)) || (t->h & (t->h-1))) return 0;
    int bpb = t->dxtfmt == 1 ? 8 : 16;
    int tw = t->w, th = t->h, off = 0;
    int size = (tw/4)*(th/4)*bpb;
    if (t->dxtlen < size) return 0;
    if (tw >= 64 && th >= 64) {
        /* Use the authored half-resolution level, matching existing detail.
         * Never expand GPU residency just because the source lacks that mip. */
        off = size; tw /= 2; th /= 2;
        size = (tw/4)*(th/4)*bpb;
        if (t->dxtlen - off < size) return 0;
    }
    *w = tw; *h = th; *offset = off; *bytes = size;
    return 1;
}

void render_dxt_plan_selftest(void) {
    unsigned char sentinel = 0; N2Tex t = {0};
    int w,h,off,bytes;
    t.dxt = &sentinel; t.dxtfmt = 1; t.w = 64; t.h = 128; t.dxtlen = 5120;
    assert(render_dxt_base_plan(&t,&w,&h,&off,&bytes));
    assert(w == 32 && h == 64 && off == 4096 && bytes == 1024);
    t.dxtlen--;
    assert(!render_dxt_base_plan(&t,&w,&h,&off,&bytes));
    t.w = 4; t.h = 8; t.dxtfmt = 3; t.dxtlen = 32;
    assert(render_dxt_base_plan(&t,&w,&h,&off,&bytes) && off == 0 && bytes == 32);
    t.dxtfmt = 5;
    assert(render_dxt_base_plan(&t,&w,&h,&off,&bytes));
    t.w = 2; assert(!render_dxt_base_plan(&t,&w,&h,&off,&bytes));
    t.w = 7; assert(!render_dxt_base_plan(&t,&w,&h,&off,&bytes));
    t.w = 4096; assert(!render_dxt_base_plan(&t,&w,&h,&off,&bytes));
    t.w = 4; t.prehalved = 1;
    assert(!render_dxt_base_plan(&t,&w,&h,&off,&bytes));
}
int   g_tex_aniso_max = 1;
float g_tex_aniso     = 1.0f;
#define GL_TEXTURE_MAX_ANISOTROPY_EXT_    0x84FE

/* Every texture this renderer owns, so a detail change can re-apply to all of
 * them. Ids are recorded on upload and validated with glIsTexture before use --
 * residents and the world cache delete textures behind our back, and setting a
 * parameter on a deleted name is a GL error, not a no-op. */
static GLuint *g_tex_all; static int g_tex_all_n, g_tex_all_cap;
static void tex_track(GLuint id) {
    if (!id) return;
    if (g_tex_all_n == g_tex_all_cap) {
        int cap = g_tex_all_cap ? g_tex_all_cap * 2 : 256;
        GLuint *t = (GLuint *)realloc(g_tex_all, (size_t)cap * sizeof *t);
        if (!t) return;                       /* out of memory: just don't track it */
        g_tex_all = t; g_tex_all_cap = cap;
    }
    g_tex_all[g_tex_all_n++] = id;
}
/* Caller has the texture bound; the id is only for symmetry with tex_track. */
static void tex_apply_aniso(void) {
    if (g_tex_aniso_max <= 1) return;
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT_, g_tex_aniso);
}
void render_texture_detail(float aniso) {
    if (g_tex_aniso_max <= 1) { g_tex_aniso = 1.0f; return; }
    if (aniso < 1.0f) aniso = 1.0f;
    if (aniso > (float)g_tex_aniso_max) aniso = (float)g_tex_aniso_max;
    if (aniso == g_tex_aniso) return;
    g_tex_aniso = aniso;
    GLint bound = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    int live = 0;
    for (int i = 0; i < g_tex_all_n; i++) {
        if (!glIsTexture(g_tex_all[i])) continue;      /* freed since upload */
        g_tex_all[live++] = g_tex_all[i];
        glBindTexture(GL_TEXTURE_2D, g_tex_all[i]);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT_, g_tex_aniso);
    }
    g_tex_all_n = live;                                 /* compact away dead ids */
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
}

#ifdef __vita__
/* Patched vitaGL (vita/lib): uploads a whole compressed mip chain in one
   allocation. Weak so the stock library (VGL=stock) still links; then the
   single-level path below is used. */
extern GLboolean vglCompressedTexImageChainUG2(GLenum internalFormat, GLsizei width,
                                               GLsizei height, GLint levels,
                                               const void *data) __attribute__((weak));
/* 1 = DXT world textures get their stored mip chain (down to 4x4). Off when
   ux0:data/NFSU2/nomips.txt exists, for A/B checks without a rebuild. */
static int g_tex_mips = -1;
long g_tex_mip_count, g_tex_mip_bytes, g_tex_flat_count;
static int tex_mips_enabled(void) {
    if (g_tex_mips < 0) {
        FILE *f = fopen("ux0:data/NFSU2/nomips.txt", "rb");
        g_tex_mips = (f == NULL) && vglCompressedTexImageChainUG2 != NULL;
        if (f) fclose(f);
        printf("texture mips: %s\n", g_tex_mips ? "DXT chains to 4x4 (LINEAR_MIPMAP_NEAREST)"
               : vglCompressedTexImageChainUG2 ? "off (nomips.txt)" : "off (stock vitaGL)");
    }
    return g_tex_mips;
}
/* Levels available from the planned base level down to 4x4. */
static int dxt_chain_levels(const N2Tex *t, int w, int h, int offset) {
    int bpb = t->dxtfmt == 1 ? 8 : 16, n = 0, off = offset;
    while (w >= 4 && h >= 4 && n < 12) {
        int sz = (w/4) * (h/4) * bpb;
        if (off + sz > t->dxtlen) break;
        off += sz; n++; w /= 2; h /= 2;
    }
    return n;
}
#endif

GLuint upload_tpk_texture_to_gpu(const N2Tex *t) {
#ifdef __vita__
    int dw, dh, offset, bytes;
    if (g_tex_s3tc && render_dxt_base_plan(t, &dw, &dh, &offset, &bytes)) {
        GLenum fmt = t->dxtfmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                   : t->dxtfmt == 3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                                   : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        GLuint id = 0; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        int levels = tex_mips_enabled() ? dxt_chain_levels(t, dw, dh, offset) : 0;
        if (levels > 1) {
            /* Filter first: the chain upload sets the GXM mip count from it.
               Bilinear within the nearest level -- trilinear is half rate. */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (vglCompressedTexImageChainUG2(fmt, dw, dh, levels, t->dxt + offset) &&
                vglGetTexDataPointer(GL_TEXTURE_2D)) {
                int bpb = t->dxtfmt == 1 ? 8 : 16, w = dw, h = dh;
                for (int l = 0; l < levels; l++, w /= 2, h /= 2)
                    g_tex_mip_bytes += (long)(w/4) * (h/4) * bpb;
                g_tex_mip_count++;
                return id;
            }
            /* allocation failed: fall through to the single level */
        }
        g_tex_flat_count++;
        /* Exactly one level: no compressed mip reallocation or sub-block tails. */
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt, dw, dh, 0, bytes, t->dxt + offset);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (!vglGetTexDataPointer(GL_TEXTURE_2D) || glGetError() != GL_NO_ERROR) {
            if (id) glDeleteTextures(1, &id);
            return 0;
        }
        return id;
    }
    return t->rgb ? upload_tex(t) : 0;
#else
    if (g_tex_s3tc && t->dxtfmt && t->dxt && t->dxtlen > 0) {
        GLenum fmt = (t->dxtfmt == 3) ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                   : (t->dxtfmt == 5) ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
                                      : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        int bpb = (t->dxtfmt == 1) ? 8 : 16;   /* S3TC block bytes (DXT3/DXT5 = 16) */
        GLuint id = 0; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        /* Replay every complete mip level in the blob (level 0 = base .. 1x1).
           Per-level block count matches n2_mipbytes2 exactly so the offsets line
           up. Only whole levels are uploaded; a chain that reaches 1x1 is a
           complete pyramid (mipmap filtering legal), otherwise fall back to a
           base-only LINEAR filter -- which only ever samples level 0, so a short
           chain can never leave the texture incomplete/black. */
        int lw = t->w, lh = t->h, off = 0, level = 0, complete = 0;
#ifdef __vita__
        /* PS Vita: (1) skip the full-size level of 64px+ textures -- a quarter
           of the texture memory, which is what lets the world fit in VRAM at
           all; (2) stop the chain at 4x4. vitaGL sizes its mip-chain buffer
           with sub-block levels smaller than one DXT block, so uploading the
           2x2/1x1 levels wrote a block past the end of the allocation and
           corrupted the heap once textures spilled out of the GPU pools. */
        int skip = (t->w >= 64 && t->h >= 64) ? 1 : 0, src_level = 0;
        for (;;) {
            int bw = lw < 4 ? 1 : lw/4, bh = lh < 4 ? 1 : lh/4;
            int sz = bw * bh * bpb;
            if (off + sz > t->dxtlen) break;
            if (lw < 4 || lh < 4) { complete = level > 0; break; }
            if (src_level >= skip) {
                glCompressedTexImage2D(GL_TEXTURE_2D, level, fmt, lw, lh, 0, sz, t->dxt + off);
                level++;
            }
            off += sz; src_level++;
            if (lw > 1) lw /= 2; if (lh > 1) lh /= 2;
        }
        if (!level) {   /* tiny (<4px) texture: one block, no chain to miscount */
            int sz = bpb;
            if (sz <= t->dxtlen)
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt, t->w, t->h, 0, sz, t->dxt);
            complete = 0;
        }
#else
        for (;;) {
            int bw = lw < 4 ? 1 : lw/4, bh = lh < 4 ? 1 : lh/4;
            int sz = bw * bh * bpb;
            if (off + sz > t->dxtlen) break;
            glCompressedTexImage2D(GL_TEXTURE_2D, level, fmt, lw, lh, 0, sz, t->dxt + off);
            off += sz; level++;
            if (lw == 1 && lh == 1) { complete = 1; break; }
            if (lw > 1) lw /= 2; if (lh > 1) lh /= 2;
        }
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        complete ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (complete) { tex_apply_aniso(); tex_track(id); }   /* needs a mip chain */
        return id;
    }
    return upload_tex(t);   /* portable fallback: CPU-decoded RGBA + mipmaps */
#endif
}

#ifdef __vita__
void render_prehalve_tex(N2Tex *t) {
    int w = t->w, h = t->h;
    if (t->prehalved || !t->rgb || w < 64 || h < 64 || (w & 1) || (h & 1)) return;
    int hw = w / 2, hh = h / 2;
    unsigned char *hr = (unsigned char *)malloc((size_t)hw * hh * 3);
    unsigned char *ha = t->alpha ? (unsigned char *)malloc((size_t)hw * hh) : NULL;
    if (!hr || (t->alpha && !ha)) { free(hr); free(ha); return; }
    for (int y = 0; y < hh; y++)
        for (int x = 0; x < hw; x++) {
            long a = (long)(2*y) * w + 2*x, b = a + 1, c = a + w, d = c + 1, o = (long)y * hw + x;
            for (int k = 0; k < 3; k++)
                hr[o*3+k] = (unsigned char)((t->rgb[a*3+k] + t->rgb[b*3+k] +
                                             t->rgb[c*3+k] + t->rgb[d*3+k] + 2) >> 2);
            if (ha) ha[o] = (unsigned char)((t->alpha[a] + t->alpha[b] +
                                             t->alpha[c] + t->alpha[d] + 2) >> 2);
        }
    free(t->rgb); free(t->alpha);
    t->rgb = hr; t->alpha = ha; t->w = hw; t->h = hh; t->prehalved = 1;
}
#endif

GLuint upload_tex(const N2Tex *t) {
    GLuint id = 0; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    int w = t->w, h = t->h;
    const unsigned char *rgb = t->rgb, *alpha = t->alpha;
#ifdef __vita__
    /* PS Vita: halve 64px+ textures (2x2 box filter) before upload. Most
       world textures are 8-bit palettized in the data, so they arrive here
       as full RGB and, uncompressed, filled the whole of VRAM by themselves.
       Same trade as dropping the top level of the DXT ones. */
    unsigned char *half_rgb = NULL, *half_alpha = NULL;
    if (rgb && !t->prehalved && w >= 64 && h >= 64 && !(w & 1) && !(h & 1)) {
        int hw = w / 2, hh = h / 2;
        half_rgb = (unsigned char *)malloc((size_t)hw * hh * 3);
        half_alpha = alpha ? (unsigned char *)malloc((size_t)hw * hh) : NULL;
        if (half_rgb && (!alpha || half_alpha)) {
            for (int y = 0; y < hh; y++)
                for (int x = 0; x < hw; x++) {
                    long a = (long)(2*y) * w + 2*x, b = a + 1, c = a + w, d = c + 1;
                    long o = (long)y * hw + x;
                    for (int k = 0; k < 3; k++)
                        half_rgb[o*3+k] = (unsigned char)((rgb[a*3+k] + rgb[b*3+k] +
                                                           rgb[c*3+k] + rgb[d*3+k] + 2) / 4);
                    if (half_alpha)
                        half_alpha[o] = (unsigned char)((alpha[a] + alpha[b] +
                                                         alpha[c] + alpha[d] + 2) / 4);
                }
            rgb = half_rgb; alpha = half_alpha; w = hw; h = hh;
        }
    }
#endif
    if (alpha) {   /* interleave the decal-mask plane -> RGBA */
        unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
        if (!px) {
            glDeleteTextures(1, &id);
#ifdef __vita__
            free(half_rgb); free(half_alpha);
#endif
            return 0;
        }
        for (long p = 0; p < (long)w * h; p++) {
            px[p*4]=rgb[p*3]; px[p*4+1]=rgb[p*3+1];
            px[p*4+2]=rgb[p*3+2]; px[p*4+3]=alpha[p];
        }
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
        free(px);
    } else
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,rgb);
#ifdef __vita__
    free(half_rgb); free(half_alpha);
#endif
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    tex_apply_aniso(); tex_track(id);
    return id;
}

/* Minimal 3x5 bitmap font (uppercase, digits, _ and -), 5 rows x 3 bits each
   (bit 2 = left column). Rendered as one unit-quad per lit pixel — fine for the
   handful of short labels in the menu. Index: 0=space, 1-10='0'-'9', 11-36=A-Z,
   37='_', 38='-'. */
static const unsigned char FONT3x5[][5] = {
    {0,0,0,0,0},                                     /* space */
    {7,5,5,5,7},{2,2,2,2,2},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1}, /* 0 1 2 3 4 */
    {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7}, /* 5 6 7 8 9 */
    {7,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,6,4,7}, /* A B C D E */
    {7,4,6,4,4},{7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7}, /* F G H I J */
    {5,5,6,5,5},{4,4,4,4,7},{5,7,5,5,5},{5,7,7,7,5},{7,5,5,5,7}, /* K L M N O */
    {7,5,7,4,4},{7,5,5,7,1},{7,5,6,5,5},{7,4,7,1,7},{7,2,2,2,2}, /* P Q R S T */
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2}, /* U V W X Y */
    {7,1,2,4,7},                                     /* Z */
    {0,0,0,0,7},{0,0,7,0,0},{1,1,2,4,4},             /* _ - / */
};
static const unsigned char *glyph3x5(char c) {
    if (c >= '0' && c <= '9') return FONT3x5[1 + (c-'0')];
    if (c >= 'A' && c <= 'Z') return FONT3x5[11 + (c-'A')];
    if (c == '_') return FONT3x5[37];
    if (c == '-') return FONT3x5[38];
    if (c == '/') return FONT3x5[39];
    return FONT3x5[0];
}
/* Draw an uppercase string at NDC (x,y = top-left), pixel size (px,py). Colour +
   uUnlit/uUseTex are set by the caller; this only sets uMVP and draws pixels. */
void draw_text(GpuMesh *quad, GLint uMVP, const char *s, float x, float y, float px, float py) {
    for (; *s; s++) {
        const unsigned char *g = glyph3x5(*s);
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (g[row] & (4 >> col)) {
                    float M[16] = { px*0.85f,0,0,0, 0,py*0.85f,0,0, 0,0,1,0,
                                    x + col*px, y - row*py, 0, 1 };
                    glUniformMatrix4fv(uMVP, 1, GL_FALSE, M);
                    draw_gpumesh(quad);
                }
        x += 4*px;   /* 3 columns + 1 gap */
    }
}
float text_width(const char *s, float px) { return (float)strlen(s) * 4 * px; }

#ifdef __vita__
/* ---- Rear-view mirror (eView 3, EVIEW_PLAYER1_RVM) ------------------------
 * PC: the view renders into its own target (0x5ba1a0: CreateTexture 256x256
 * A8R8G8B8 render target + D16 depth, 0x7fee80/84/78/7c); after the world,
 * the cars and the effects (0x5caad0 view 3) the "RVM" texture is laid over
 * the whole target alpha blended (0x5c27b0 SRCALPHA / INVSRCALPHA) and
 * "RVM_MASK" written with ONE / ZERO (technique 8: taken as the target's
 * alpha); 0x5cbf40 then draws the target alpha blended (0x5bcbc0(1, 5, 6))
 * as one poly over x 440..200, y 15..95 of the 640x480 screen -- the first
 * corner on the right with u = 0, so the picture is flipped left to right
 * like a mirror (GC DisplayRVMs 801da75c: 224..416 x 20..84, u 1 -> 0). */
static struct { GLuint fbo, tex, depth; int w, h, failed; } g_rvm;
int render_rvm_begin(int w, int h, const float clear_rgb[3]) {
    if (g_rvm.failed) return 0;
    if (!g_rvm.fbo || g_rvm.w != w || g_rvm.h != h) {
        if (g_rvm.fbo) { glDeleteFramebuffers(1, &g_rvm.fbo); glDeleteTextures(1, &g_rvm.tex);
                         glDeleteRenderbuffers(1, &g_rvm.depth); g_rvm.fbo = 0; }
        glGenTextures(1, &g_rvm.tex);
        glBindTexture(GL_TEXTURE_2D, g_rvm.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &g_rvm.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_rvm.fbo);
        glGenRenderbuffers(1, &g_rvm.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, g_rvm.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_rvm.depth);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_rvm.tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            printf("rear-view mirror unavailable (framebuffer incomplete)\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g_rvm.failed = 1;
            return 0;
        }
        g_rvm.w = w; g_rvm.h = h;
        printf("rear-view mirror target %dx%d\n", w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_rvm.fbo);
    glViewport(0, 0, w, h);
    glClearColor(clear_rgb[0], clear_rgb[1], clear_rgb[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return 1;
}
void render_rvm_end(int screen_w, int screen_h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w, screen_h);
}
GLuint render_rvm_texture(void) { return g_rvm.failed ? 0 : g_rvm.tex; }

static const char *RVM_FS =
    GLSL_HEADER
    "varying vec2 vUV; uniform sampler2D uTex; uniform sampler2D uOver; uniform sampler2D uMask;\n"
    "uniform float uHaveOver; uniform float uHaveMask;\n"
    "void main(){\n"
    "  vec2 t = vec2(1.0 - vUV.x, vUV.y);\n"          /* the mirror flip */
    "  vec2 m = vec2(t.x, 1.0 - t.y);\n"              /* TPK art: top-left origin */
    "  vec3 c = texture2D(uTex, t).rgb;\n"
    "  vec4 o = texture2D(uOver, m);\n"
    "  c = mix(c, o.rgb, o.a * uHaveOver);\n"
    "  float a = mix(1.0, texture2D(uMask, m).a, uHaveMask);\n"
    "  gl_FragColor = vec4(c, a);\n"
    "}\n";
static GLuint g_rvm_prog;
void render_rvm_composite(GpuMesh *quad, GLuint over, GLuint mask, int x, int y, int w, int h,
                          int screen_w, int screen_h) {
    if (!g_rvm_prog) {
        GLuint p = glCreateProgram();
        glAttachShader(p, compile(GL_VERTEX_SHADER, BLIT_VS));
        glAttachShader(p, compile(GL_FRAGMENT_SHADER, RVM_FS));
        glBindAttribLocation(p, 0, "aPos");
        glBindAttribLocation(p, 1, "aUV");
        glLinkProgram(p);
        GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        g_rvm_prog = ok ? p : (GLuint)-1;
        printf("rear-view mirror program: %s\n", ok ? "ok" : "link failed");
    }
    if (g_rvm_prog == (GLuint)-1 || !quad || g_rvm.failed || !g_rvm.tex) return;
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(x, y, w, h);
    glUseProgram(g_rvm_prog);
    glUniform1i(glGetUniformLocation(g_rvm_prog, "uTex"), 0);
    glUniform1i(glGetUniformLocation(g_rvm_prog, "uOver"), 1);
    glUniform1i(glGetUniformLocation(g_rvm_prog, "uMask"), 2);
    glUniform1f(glGetUniformLocation(g_rvm_prog, "uHaveOver"), over ? 1.0f : 0.0f);
    glUniform1f(glGetUniformLocation(g_rvm_prog, "uHaveMask"), mask ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, mask ? mask : g_rvm.tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, over ? over : g_rvm.tex);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_rvm.tex);
    draw_gpumesh(quad);
    glViewport(0, 0, screen_w, screen_h);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (!blend) glDisable(GL_BLEND);
}
#endif
