/* C4b career opponents: AddCareerEventAIOpponents (airace_career) over
   events of every stage, at the stage's lowest and highest AI level, for a
   stock player 350Z: the cars, ratings and classes should climb with the level.
   usage: t_copp [seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "career.h"
#include "airace.h"
#include "perfparts.h"

static unsigned char *rd(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b;
}
static unsigned u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

int main(int argc, char **argv)
{
    unsigned seed = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
    long gl; unsigned char *g = rd("E:/NFS Underground 2 Vita/data/nfsug2/GLOBAL/GLOBALB.BUN", &gl);
    if (!g || !career_init(g, gl)) { printf("no career data\n"); return 1; }
    if (airace_types_init(g, gl) < 31 || !perf_init(g, gl)) { printf("no car types / perf data\n"); return 1; }
    int ptype = airace_type_of("350Z");
    PerfParts pp; perf_clear(&pp);
    unsigned char pb[N2_REC_SIZE];
    perf_build(airace_type_rec(ptype), &pp, pb);
    printf("player 350Z stock: rating %.0f level %.2f\n", perf_rating(pb), perf_level(perf_rating(pb)));
    unsigned random = airace_hash("RANDOM");
    int bad = 0, shown[6] = { 0 };
    for (int i = 0; i < career_nevents(); i++) {
        const unsigned char *ev = career_event(i);
        int st = ev[0x10], n = (signed char)ev[0x7c];
        if (st > 5 || n <= 0 || shown[st] >= 2 || ev[0x42] != 3) continue;
        shown[st]++;
        const unsigned char *sd = career_stage_data(st);
        float lo, hi; memcpy(&lo, sd + 0x44, 4); memcpy(&hi, sd + 0x48, 4);
        AiCareerSlot sl[5];
        for (int k = 0; k < n && k < 5; k++) {
            const unsigned char *o = ev + 0x40 + 12 * k;
            sl[k].name = career_string((unsigned short)(o[0] | o[1] << 8));
            sl[k].mode = (signed char)o[2]; sl[k].level = (signed char)o[3];
            sl[k].type = u32(o + 4) == random ? -1 : 0;
            sl[k].skill = (signed char)o[9]; sl[k].block = (signed char)o[0xb];
        }
        for (int pass = 0; pass < 2; pass++) {
            float L = pass ? hi : lo;
            AiRng rng = { seed };
            AiOpponent opp[8];
            int m = airace_career(n, sl, ev[0xd] == 1, L, pb, &pp, ev[0xf] == 4, &rng, opp);
            printf("stage %d %-18s L %4.1f:", st, career_string((unsigned short)(ev[0] | ev[1] << 8)), L);
            float sum = 0;
            for (int k = 0; k < m; k++) {
                printf(" %s %s %.0f c%d s%d |", opp[k].name, opp[k].car, opp[k].rating, opp[k].klass, opp[k].skill);
                sum += opp[k].rating;
                if (!(opp[k].rating > 0) || opp[k].type < 0) bad++;
            }
            printf("  avg %.0f\n", m ? sum / m : 0);
        }
    }
    printf("%s (%d bad)\n", bad ? "FAIL" : "ok", bad);
    return bad != 0;
}
