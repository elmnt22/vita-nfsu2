/* psp2 stand-in types for the Windows simulator (sim/sim_vita.c). */
#ifndef SIM_PSP2_TYPES_H
#define SIM_PSP2_TYPES_H
#include <stdint.h>
typedef int SceUID;
typedef unsigned int SceSize;
typedef int64_t SceOff;
typedef uint64_t SceUInt64;
typedef int SceMode;
#ifdef __cplusplus
extern "C" {
#endif
SceUInt64 sceKernelGetProcessTimeWide(void);
int sceClibPrintf(const char *fmt, ...);
SceUID sceIoOpen(const char *path, int flags, SceMode mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *buf, SceSize n);
int sceIoPread(SceUID fd, void *buf, SceSize n, SceOff off);
int sceIoMkdir(const char *path, SceMode mode);
#ifdef __cplusplus
}
#endif
#endif
