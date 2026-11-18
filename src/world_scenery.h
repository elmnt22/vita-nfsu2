#ifndef WORLD_SCENERY_H
#define WORLD_SCENERY_H
#include "world_group_reader.h"
#include <stdio.h>

/* Conservative load-time preview, NOT decoded retail activation flags.
 * Only exclusive membership in numeric event groups is suppressible. Unknown,
 * career, free-roam and shared memberships retain their previous behavior. */
typedef struct {
    uint16_t section, row, flags;
    unsigned char membership, checked;
} WGSelected;
typedef struct { WGSelected *items; size_t count; } WGSelection;
enum { WG_EVENT=1, WG_OTHER=2, WG_ACTIVE=4, WG_LOCK=8 };

/* Career scenery groups (GC TrackLoader::InitTopologyAndSceneryGroups
 * 80196cb8). The retail loader enables, only when not racing (race params
 * +0x14 == 0): in career mode (FEDatabase mode 1) "FREE_ROAM" (stage > 0), else
 * "FREE_ROAM_STAGE0" once a career car is owned (PlayerCareerState +0x7e2c,
 * CommitChooseCar / CommitSwitchToOwnedCar), else "FREE_ROAM_STAGE0A"; and
 * always "BARRIERS_CAREER<stage>" (solid too, EnableBarriers(.., 1)).
 * Everything else here stays disabled: races show none of them, and
 * BARRIERS_DEMO is a demo-build group.
 * g_wg_career_stage < 0 = no career (the port's own debug free roam): the
 * city stays fully open as before (lock walls hidden, FREE_ROAM* shown). */
extern int g_wg_career_stage, g_wg_career_owns_car;
/* (port) expansion city (MW Rockport): race barriers are MW's BARRIER_SPLINE_<n> groups
 * (xpconv xp_companion), shared between races; each race names its own (MW race_bin
 * field 0xe244f26b -> TRACKS/ROUTES<region>/barriers.txt). The hook (main.c xpbar_on,
 * NULL outside an expansion city) says: 1 the event's own, 0 not (another race's, or
 * free roam: MW shows none there), -1 the event has no barrier list. */
extern int (*g_wg_xp_barrier_on)(const char *name,int event);
static int wg_xp_barrier(const char *name) { return !strncmp(name,"BARRIER_SPLINE_",15); }
static int wg_lock_group(const char *name) {
    return !strncmp(name,"BARRIERS_CAREER",15) || !strcmp(name,"BARRIERS_DEMO");
}
static int wg_roam_group(const char *name) { return !strncmp(name,"FREE_ROAM",9); }
/* For a career or free-roam group: 1 enabled, 0 disabled, -1 not one of them. */
static int wg_career_group_on(const char *name,int event) {
    int lock=wg_lock_group(name), roam=wg_roam_group(name);
    if(!lock&&!roam)return -1;
    if(g_wg_career_stage<0)return roam;            /* no career: unchanged */
    if(event!=-1)return 0;                           /* racing / previews */
    char want[24];
    if(lock){ snprintf(want,sizeof want,"BARRIERS_CAREER%d",g_wg_career_stage); return !strcmp(name,want); }
    return !strcmp(name,g_wg_career_stage?"FREE_ROAM":g_wg_career_owns_car?"FREE_ROAM_STAGE0":"FREE_ROAM_STAGE0A");
}

/* Runtime policy remains deliberately narrower than retail semantics. Normal
 * free roam hides only placements proven exclusive to numeric event groups.
 * A requested preview keeps its explicit selection. A live race selects its
 * OWN authored group, so another event's road closures no longer stand on the
 * raced route. Direction/career activation timing is still undecoded: within
 * the selected event nothing is further filtered.
 *
 * Measured on STREAML4RG event 4701 (M160): 4827 group placements are loaded,
 * only 77 of them belong to BARRIERS_4701, and foreign-exclusive placements
 * including two other events' start/finish gantries sit at 0.00 m from 4701's
 * own 0x34148 racing line.
 *
 * A positive id here is a REQUEST. Some shipped events author no group at all
 * (L4RB 4201/4202/4203, L4RC 4341), and the builder degrades those back to the
 * unfiltered scene rather than failing to load. */
static int wg_runtime_selection(int preview_set,int preview_event,int race_event) {
    if(preview_set)return preview_event;
    return race_event>0?race_event:-1;
}

static int wg_event_id(const char *name) {
    const char *p=NULL;
    if(!strncmp(name,"BARRIERS_",9))p=name+9;
    else if(!strncmp(name,"PLAYER_BARRIERS_",16))p=name+16;
    if(!p||!*p)return 0;
    unsigned n=0;
    for(;*p;p++) {
        if(*p<'0'||*p>'9'||n>6553)return 0;
        n=n*10+(unsigned)(*p-'0');
    }
    return n>0&&n<=65535?(int)n:0;
}
/* Does this bundle author a group for the requested event? Free roam (-1) and
 * "unchanged" (0) are always answerable, so only a positive id is looked up.
 * Absence is an authoring fact, NOT a malformed table: the caller uses this to
 * tell the two apart instead of treating both as a load failure. */
static int wg_event_group_present(const WGTable *table,int event) {
    if(!table)return 0;
    if(event<=0)return 1;
    if(g_wg_xp_barrier_on)return g_wg_xp_barrier_on(NULL,event)>=0;
    for(size_t p=0;p<table->group_bytes;) {
        const unsigned char *g=table->groups+p;
        uint32_t refs=wg_u32(g+48);
        if(wg_event_id((const char *)g+8)==event)return 1;
        p+=(size_t)((52ULL+2ULL*refs+3)&~3ULL);
    }
    return 0;
}
static int wg_selected_cmp(const void *aa,const void *bb) {
    const WGSelected *a=aa,*b=bb;
    if(a->section!=b->section)return a->section<b->section?-1:1;
    return (a->row>b->row)-(a->row<b->row);
}
static int wg_selection_open(const WGTable *table,int event,WGSelection *out) {
    memset(out,0,sizeof *out);
    if(event < -1 || event > 65535)return 0;
    if(!event)return 1;
    size_t n=table->override_count;
    WGSelected *items=n?calloc(n,sizeof *items):NULL;
    if(n&&!items)return 0;
    for(size_t i=0;i<n;i++) {
        const unsigned char *r=table->overrides+8*i;
        items[i].section=wg_u16(r);items[i].row=wg_u16(r+2);items[i].flags=wg_u16(r+4);
    }
    int found=event==-1;
    for(size_t p=0;p<table->group_bytes;) {
        const unsigned char *g=table->groups+p;
        int id=wg_event_id((const char *)g+8);
        int cg=wg_career_group_on((const char *)g+8,event);
        unsigned bits=id?WG_EVENT:cg>0?WG_OTHER:cg==0?WG_LOCK:WG_OTHER;
        if(id>0&&id==event){bits|=WG_ACTIVE;found=1;}
        if(g_wg_xp_barrier_on&&wg_xp_barrier((const char *)g+8)) {
            bits=WG_EVENT;
            if(event>0&&g_wg_xp_barrier_on((const char *)g+8,event)==1){bits|=WG_ACTIVE;found=1;}
        }
        uint32_t refs=wg_u32(g+48);
        for(uint32_t j=0;j<refs;j++)items[wg_u16(g+52+2*(size_t)j)].membership|=(unsigned char)bits;
        p+=(size_t)((52ULL+2ULL*refs+3)&~3ULL);
    }
    if(n>1)qsort(items,n,sizeof *items,wg_selected_cmp);
    for(size_t i=1;i<n;i++)if(!wg_selected_cmp(items+i-1,items+i)){free(items);return 0;}
    if(!found&&g_wg_xp_barrier_on&&g_wg_xp_barrier_on(NULL,event)>=0)found=1;   /* a list whose groups this city lacks */
    if(!found){free(items);return 0;}
    out->items=items;out->count=n;return 1;
}
static const WGSelected *wg_selection_find(const WGSelection *s,unsigned section,unsigned row) {
    if(!s||!s->count||section>65535||row>65535)return NULL;
    WGSelected key={0};key.section=(uint16_t)section;key.row=(uint16_t)row;
    return bsearch(&key,s->items,s->count,sizeof *s->items,wg_selected_cmp);
}
static int wg_selection_visible(const WGSelection *s,unsigned section,unsigned row) {
    const WGSelected *i=wg_selection_find(s,section,row);
    if (!i) return 1;
    /* the selected race's own group, or any ordinary group, keeps it */
    if (i->membership&(WG_OTHER|WG_ACTIVE)) return 1;
    /* only other events' closures and/or career locks: hidden */
    return !(i->membership&(WG_EVENT|WG_LOCK));
}
#endif
