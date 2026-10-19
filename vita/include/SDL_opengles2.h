/* SDL_opengles2.h — OpenUG2 PS Vita GL header: vitaGL plus the two gaps the
 * engine hits on it.
 *
 *  - glGetUniformfv: vitaGL has no uniform readback, but the renderer saves
 *    and restores uniforms around the HUD/headlight passes. Every glUniform*
 *    write is mirrored into a (program, location) shadow cache in sdl_vita.c
 *    and glGetUniformfv answers from that cache.
 *  - GL_DITHER: not an enum vitaGL knows; enable/disable/query are no-ops.
 */
#ifndef OPENUG2_VITA_GLES2_H
#define OPENUG2_VITA_GLES2_H

#include <vitaGL.h>

#ifndef GL_DITHER
#define GL_DITHER 0x0BD0
#endif

#ifdef __cplusplus
extern "C" {
#endif
void      vu_UseProgram(GLuint prog);
GLint     vu_Uniform(GLint loc, int n, int count, const GLfloat *v);
/* Vita world pass: run a lighter program under the main program's uniform
   locations (writes are cached under the main program and translated). */
void      vita_alias_program(GLuint main_prog, GLuint lite_prog, const char *const *names, int n);
void      vita_use_lite(int on);
/* Specialised variants of the main program, picked per draw (see sdl_vita.c). */
int       vita_add_variant(GLuint prog, int key, const char *const *names, int n);
void      vu_select(void);
void      vu_GetUniformfv(GLuint prog, GLint loc, GLfloat *out);
void      vu_LinkProgram(GLuint prog);
void      vu_Enable(GLenum cap);
void      vu_Disable(GLenum cap);
GLboolean vu_IsEnabled(GLenum cap);
/* depth of field: blended draws get the alpha write masked off (sdl_vita.c) */
void      vu_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void      vu_GetBooleanv(GLenum pname, GLboolean *out);
void      vita_set_dof(int on);
#ifdef __cplusplus
}
#endif

static inline void vu_glUniform1f(GLint l, GLfloat a) { GLfloat v[1] = {a}; l = vu_Uniform(l, 1, 1, v); if (l != -1) glUniform1f(l, a); }
static inline void vu_glUniform1i(GLint l, GLint a) { GLfloat v[1] = {(GLfloat)a}; l = vu_Uniform(l, 1, 1, v); if (l != -1) glUniform1i(l, a); }
static inline void vu_glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { GLfloat v[3] = {a, b, c}; l = vu_Uniform(l, 3, 1, v); if (l != -1) glUniform3f(l, a, b, c); }
static inline void vu_glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { GLfloat v[4] = {a, b, c, d}; l = vu_Uniform(l, 4, 1, v); if (l != -1) glUniform4f(l, a, b, c, d); }
static inline void vu_glUniform3fv(GLint l, GLsizei n, const GLfloat *v) { l = vu_Uniform(l, 3, n, v); if (l != -1) glUniform3fv(l, n, v); }
static inline void vu_glUniform4fv(GLint l, GLsizei n, const GLfloat *v) { l = vu_Uniform(l, 4, n, v); if (l != -1) glUniform4fv(l, n, v); }
static inline void vu_glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v) { l = vu_Uniform(l, 16, n, v); if (l != -1) glUniformMatrix4fv(l, n, t, v); }
static inline void vu_glUseProgram(GLuint p) { glUseProgram(p); vu_UseProgram(p); }
static inline void vu_glDrawElements(GLenum m, GLsizei c, GLenum t, const void *i) { vu_select(); glDrawElements(m, c, t, i); }
static inline void vu_glDrawArrays(GLenum m, GLint f, GLsizei c) { vu_select(); glDrawArrays(m, f, c); }

#define glUniform1f        vu_glUniform1f
#define glUniform1i        vu_glUniform1i
#define glUniform3f        vu_glUniform3f
#define glUniform4f        vu_glUniform4f
#define glUniform3fv       vu_glUniform3fv
#define glUniform4fv       vu_glUniform4fv
#define glUniformMatrix4fv vu_glUniformMatrix4fv
#define glUseProgram       vu_glUseProgram
#define glDrawElements     vu_glDrawElements
#define glDrawArrays       vu_glDrawArrays
#define glGetUniformfv     vu_GetUniformfv
#define glLinkProgram      vu_LinkProgram
#define glEnable           vu_Enable
#define glDisable          vu_Disable
#define glIsEnabled        vu_IsEnabled
#define glColorMask        vu_ColorMask
#define glGetBooleanv      vu_GetBooleanv

#endif
