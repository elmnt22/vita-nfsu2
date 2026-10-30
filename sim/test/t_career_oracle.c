/* the career save against SPEED2.EXE: re-tools/out/career/oracle/save_oracle.py
   runs PlayerCareerState::SaveToBuffer 0x514530 on a filled object; loading
   those bytes and saving them again must give the same bytes and fields. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "career.h"

static unsigned char *rd(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b;
}

int main(void)
{
    long gl, n; unsigned char *g = rd("E:/NFS Underground 2 Vita/data/nfsug2/GLOBAL/GLOBALB.BUN", &gl);
    unsigned char *pc = rd("E:/re-tools/out/career/oracle/pc_save.bin", &n);
    if (!g || !pc || !career_init(g, gl)) { printf("no data (run save_oracle.py)\n"); return 1; }
    int bad = 0;
    long want = career_save(NULL, 0);
    printf("PC save %ld bytes, ours %ld\n", n, want);
    if (n != want) bad++;
    else {
        if (!career_load(pc, n)) { printf("load failed\n"); bad++; }
        CareerState *c = &g_career;
        printf("cash %d stage %d nrec %d crib_in_event %d u872c %08x b873e/f %d/%d started/d %d/%d hash %08x has_spawn %d spawn_heading %04x f8764 %g ev %08x\n",
               c->cash, c->stage, c->nrec, c->crib_in_event, c->u872c, c->b873e, c->b873f, c->started, c->b873d,
               c->career_hash, c->has_spawn, c->spawn_heading, c->f8764, c->current_event);
        if (c->cash != 123456 || c->stage != 3 || c->nrec != 0x40 || c->u872c != 0xdeadbeefu || c->career_hash != 0x11223344u ||
            c->spawn_heading != 0x5566 || c->f8764 != -1.0f || c->current_event != 0x808b7899u || c->started != 2 || c->b873d != 3 || c->has_spawn != 5) bad++;
        int nsp = 0;
        for (int s = 0; s < 8; s++) for (int k = 0; k < 5; k++) nsp += c->st[s].chosen[k] != NULL;
        printf("sponsors resolved: %d\n", nsp);
        unsigned char *b = malloc(n);
        career_save(b, n);
        long d = 0; while (d < n && b[d] == pc[d]) d++;
        if (d < n) { printf("first difference at byte %ld (%02x vs PC %02x)\n", d, b[d], pc[d]); bad++; }
        else printf("re-save identical to SPEED2.EXE's\n");
    }
    printf("%s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
