/* partanim.h -- the opening car parts: hood, trunk and doors (SPEED2.EXE
 * 0x433a30..0x434b00, driven by 0x61f820, drawn by 0x620ae0).
 *
 * Each car's CARS/<CAR>/PARTS_ANIMATIONS.bin holds EAGL animation banks
 * (chunk 0xe34010) with "<CAR>_ZAM_<PART>_q" / "_t" containers (type 15)
 * of raw keyframes (sub type 0: header 0x10 + 8 per field, then frames of
 * x y z w quaternions or x y z pivots, 30 per second), and chunk 0x37100:
 * the 21 rest matrices (hood styles 0..5, left door 6..8, right door 9..11,
 * trunk 12, left wide doors 13..16, right 17..20; 0x433f40), whose
 * translation stands in when a _t channel is static.
 *
 * The part's matrix at time t is rotation(q(t)) + pivot(t); a mesh riding
 * on it is drawn through rest^-1 * that (0x434460 / 0x434190 in 0x620ae0). */
#ifndef OPENUG2_PARTANIM_H
#define OPENUG2_PARTANIM_H

#ifdef __cplusplus
extern "C" {
#endif

enum { PA_HOOD, PA_TRUNK, PA_LEFT_DOOR, PA_RIGHT_DOOR, PA_PARTS };

/* hood styles (0x433ad0): 0 REG, 1 FRONT, 2 LEFT_SPLIT, 3 RIGHT_SPLIT,
   4 FRONT_SPLIT, 5 BACK_SPLIT; door styles: 0 REG, 1 SCISSOR, 2 SUICIDE,
   3..6 WIDE1..4 */
typedef struct PartAnims PartAnims;

/* Load a car's animations (NULL when the car has none). */
PartAnims *partanim_load(const char *dataroot, const char *car);
void partanim_free(PartAnims *pa);

/* 0x61f820 -> 0x433e20: ask a part to open (1) or close (0). style as
   above (the hood's split style, the doors' ANIMSTYLE / wide style); flag:
   the STYLE22 hood (its halves at 0.75 and 1.25 speed). now = seconds. */
void partanim_request(PartAnims *pa, int part, int open, int style, int flag, float now);
/* 0x433cd0: 0 open, 1 closed (the default), 2 opening, 3 closing */
int  partanim_state(const PartAnims *pa, int part);

/* The matrix (row vectors, car space) a mesh of `part` is drawn through
   at `now`; half = a split hood's right half (0x434550: left/back 0,
   right/front 1). Returns 0 when the part is closed (identity). */
int  partanim_matrix(PartAnims *pa, int part, int half, float now, float out[16]);

#ifdef __cplusplus
}
#endif
#endif
