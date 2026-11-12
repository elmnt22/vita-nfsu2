/* garage_line.h -- one garage.txt car line (shared by the game and the
 * PC converter's save import). */
#ifndef OPENUG2_GARAGE_LINE_H
#define OPENUG2_GARAGE_LINE_H
#include <stddef.h>
#include "car_config.h"

/* a car's garage.txt line (no newline); the career slots keep the same line */
void garage_format_line(char *b, size_t cap, const char *car, const N2CarConfig *cfg, const char *rim, int rim_style);

#endif
