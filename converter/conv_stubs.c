/* conv_stubs.c -- symbols the linked game objects reference only from their
 * drawing paths (car materials, the GL uniform shims). The conversion never
 * reaches them; should it ever, it stops with the name instead of drawing
 * through a GL that is not there. No headers on purpose: only the names
 * matter to the linker. */
void conv_fatal(const char *what);
#define STUB(name) void name(void) { conv_fatal(#name); }
STUB(carfx_default) STUB(carfx_loaded) STUB(carfx_material) STUB(carfx_paint) STUB(carfx_use_paint)
STUB(vita_add_variant) STUB(vita_alias_program) STUB(vita_set_dof)
STUB(vu_ColorMask) STUB(vu_Disable) STUB(vu_Enable) STUB(vu_GetBooleanv) STUB(vu_GetUniformfv)
STUB(vu_IsEnabled) STUB(vu_LinkProgram) STUB(vu_Uniform) STUB(vu_UseProgram) STUB(vu_select)
