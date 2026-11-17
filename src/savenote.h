/* savenote.h - the "Save complete." note in the corner after a save (port
 * addition, see savenote.c) */
#ifndef OPENUG2_SAVENOTE_H
#define OPENUG2_SAVENOTE_H
void savenote_saved(void);    /* a save went through: show the note */
void savenote_failed(void);   /* an autosave did not go through: "Unable to save" */
void savenote_cancel(void);   /* a dialog already says so: no note */
/* over the finished frame, before the swap (GL thread) */
void savenote_draw(const char *dataroot, int screen_w, int screen_h);
#endif
