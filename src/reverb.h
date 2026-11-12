/* reverb.h -- the original's software reverb: EA sndlib's SFILTER graph
   (SNDMIXI_initfx / SNDMIXI_modlapifxadd) running the SOUND/FXEDIT/*.fx
   patches, chosen by SFXCTL_Tunnel (tunnels, the city's reverb parameter
   map) and SFXObj_Reverb (front end / in game). */
#ifndef OPENUG2_REVERB_H
#define OPENUG2_REVERB_H

/* SWITCH: the reverb send bus. 1 = on (retail); build with
   -DOPENUG2_REVERB=0 to drop it (the Vita mixer cost, see GAPS / the
   report). At run time g_reverb_on = 0 bypasses it too. */
#ifndef OPENUG2_REVERB
#define OPENUG2_REVERB 1
#endif

extern volatile int g_reverb_on;

/* the 12 patches (PC table 0x7ed608: Car_Show_Sml .. Alley) */
int reverb_load(const char *dataroot);
/* the track bundle's ParameterMaps chunk 0x8003b600: keeps the reverb
   layer (ReverbAccessor, layer bStringHash("Reverb") 0x79b2e325) */
void reverb_map_load(const unsigned char *bun, long len);
/* game thread, every frame: in_game 0 = the front end; car = the player's
   car (engine x, y); tunnel = the tunnel zone type under the car (0 none) */
void reverb_update(int in_game, const float car[3], int tunnel, float dt);
/* audio thread: the engines' fx send level (0x82b078 / 32767) */
float reverb_send_level(void);
/* audio thread: the active patch number (-1 none) */
int reverb_patch(void);
/* audio thread: the fx bus (mono, n samples) through the active patch
   into the two output channels (written, not added); 0 = idle (silent) */
int reverb_process(const float *bus, float *outl, float *outr, int n);

#endif
