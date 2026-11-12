/* worldanim.h -- the world animations (CAnimWorldScene / CWorldAnimEntity /
 * CWorldAnimCtrl), ported from SPEED2.EXE v1.2 with the GameCube Speed.elf
 * symbols: the jumbo planes over the city, the tower cranes, the graffiti
 * trains, the bridge and vista spotlights, the pub sails, the shop doors and
 * the drag strip trams.
 *
 * Data (the track's companion bundle, e.g. TRACKS/L4RA.BUN, top level):
 *   0x37080 WorldAnimEntity (0x70 B at the chunk's 16-aligned data): +4 name
 *     hash, +8 parent name, +0xc solid, +0x10/+0x14 extra solids, +0x20 tree,
 *     +0x24 animation base hash, +0x28 channels (1 _t, 2 _q, 4 _s), +0x29
 *     parent index (-1 the root), +0x30 local matrix;
 *   0x37110 tree marker (+0 tree hash): closes the entities before it
 *     (LoaderWorldAnimTreeMarker 0x43a260);
 *   0x37090 WorldAnimInstance (0xa8 B): +4 tree, +8 section, +0xc flags,
 *     +0x10 rate, +0x14 start delay, +0x18 loop delay, +0x1c/+0x1e loop
 *     range, +0x20/+0x24 shop hashes, +0x30 named range, +0x34 event track,
 *     +0x38 race filter, +0x3c FwdRevTrack, +0x40 matrix, +0x80/+0x90 box;
 *   0xe34010 the EAGL animation banks (nis.c decodes them).
 * The solids are WorldModels of STREAM<stem>.BUN, found by name hash in any
 * section (X0 2400 first, then the companion's section table 0x34110, as
 * eFindSolid over every list), drawn at their entity's node.
 *
 * Code: StartWorldAnimations 0x43a880 (TrackLoader::FinishedLoadingPart1)
 * -> InstantiateAnimTree 0x43a4a0 -> CWorldAnimEntity::Init 0x438860; the
 * frame 0x4321c0 -> CWorldAnimEntity::UpdateTimeStep 0x435980 ->
 * CWorldAnimCtrl AdvanceAnimTime 0x435140 / UpdateAnimPose 0x4316b0;
 * BroadcastEventToWorldAnims 0x438e30 / 0x438ca0 with the control scenarios
 * ShopDoor (DOOR_POSITION 0x435660) and GenericNIS (ANIM_POSITION 0x435750),
 * FwdRevTrack 0x435880; RenderWorldModels 0x61d4b0 -> 0x61d150. */
#ifndef OPENUG2_WORLDANIM_H
#define OPENUG2_WORLDANIM_H
#include "render.h"
#ifdef __cplusplus
extern "C" {
#endif

/* the trees, instances and banks of <troot>/<stem>.BUN and the solids they
   use from STREAM<stem>.BUN (track = "STREAML4RA"); replaces what was
   loaded. Returns the number of instances (0 = none / no data). */
int  wa_load(const char *troot, const char *track);
void wa_free(void);
/* StartWorldAnimations 0x43a880: every instance instantiated again for this
   event (race filter: event % 100) and direction (0x89e7a4, FwdRevTrack) */
void wa_start(int event, int reverse);
/* 0x4321c0: one frame of every tree. nis_playing: a scene is playing
   (0x827ec8 / 0x827e98: the shop doors pose too) */
void wa_update(float dt, int nis_playing);
/* BroadcastEventToWorldAnims 0x438e30 from a scene's WORLD_MESSAGE
   (0x4392e0 event 12): msg 1 DOOR_POSITION (to the trees of the current shop
   `shop`), 2 ANIM_POSITION (to the entities named `target`); a, b: the
   event's two floats */
void wa_message(int msg, unsigned target, float a, float b, unsigned shop);
/* RenderWorldModels 0x61d4b0: the animated models in view (planes: the six
   frustum planes {a, b, c, d}, inside >= 0; NULL draws all) */
void wa_draw(const RProg *rp, const float MVP[16], const float (*planes)[4]);
/* test aid: one entity's world position by name hash (1 when it has a model) */
int  wa_entity_pos(unsigned name, float pos[3]);
/* GetAnimPosition 0x439f40: the world position of entity `name` of the tree
   whose instance id (+0) is `id`; *delay (may be NULL) = its controller is in
   the start delay or between loops. 0 = no such tree / entity. */
int  wa_anim_position(unsigned id, unsigned name, float pos[3], int *delay);

#ifdef __cplusplus
}
#endif
#endif
