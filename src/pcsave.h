/* pcsave.h -- importing a save of the original PC game into a port profile.
 *
 * The PC game keeps a profile at %LOCALAPPDATA%\NFS Underground 2\<NAME>\<NAME>
 * (SHGetFolderPathA CSIDL_LOCAL_APPDATA, 0x4a9c40): the realmc container
 * ("20CM", 0x6fceb0) around UserProfile::SaveToBuffer's payload (PC 0x52a280,
 * GC 80131db4; version 0x10d, 0xd692 bytes). pcsave decodes that payload and
 * writes the port's own profile (profile.c's tagged blocks) for the same
 * name: options, the garage (the tuned cars, the current car), the career
 * cars (parts, paint, vinyls, inventory, tunings), the career and the high
 * scores. Used by UG2Converter (--import-save, the window) and in game by
 * the Profile Manager (a save copied to ux0:data/NFSU2/pcsave/).
 */
#ifndef OPENUG2_PCSAVE_H
#define OPENUG2_PCSAVE_H

#define PCSAVE_DIR "ux0:data/NFSU2/pcsave"   /* in game: <dir>/<NAME>/<NAME> or <dir>/<NAME> */

typedef struct {
    char name[32];          /* the profile's name (payload +0xd201) */
    int  crc_ok;            /* the payload CRC matched (the PC game calls a mismatch corrupt) */
    int  ntuned, ncareer, nhs, nown;
    int  fit_n, fit_owned;  /* the career cars' fitted upgraded parts / those the inventory owns */
    char current[32];       /* the current car's type */
    char msg[256];          /* what went wrong, or a summary */
} PcSaveInfo;

/* the name in a PC save, 1 if it is one */
int pcsave_peek(const char *savefile, char name[32]);
/* decode savefile (dataroot: the game data, GLOBAL/GLOBALB.BUN) and write
   the port profile to profdir/<NAME>/<NAME>; 1 ok */
int pcsave_import(const char *savefile, const char *dataroot, const char *profdir, PcSaveInfo *info);
/* in game: the first PC save under PCSAVE_DIR whose name has no profile in
   profdir yet, 1 found (path and name filled) */
int pcsave_find_new(const char *profdir, char *path, int cap, char name[32]);

#endif
