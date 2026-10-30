#ifndef OPENUG2_CAR_MOD_H
#define OPENUG2_CAR_MOD_H
#include "nfsu2.h"

static const char *const n2_mod_libraries[] = {
    "", "SPOILER", "SPOILER_HATCH", "SPOILER_SUV", "EXHAUST", "ROOF",
    "MIRRORS_BODY", "MIRRORS_POST", "MIRRORS_SUV", "MIRRORS_HUMMER"
};
#define N2_LIB_MIRROR0 6   /* libraries 6..9: the side mirrors */
static int n2_lib_slot(int lib) {
    return lib <= 3 ? N2_PART_SPOILER : lib == 4 ? N2_PART_EXHAUST : lib == 5 ? N2_PART_SCOOP : N2_PART_MIRROR;
}
/* the car's name: its meshes are <CAR>_KITnn_... */
static void n2_mod_car_name(const unsigned char *d,long len,const N2Scene *car,char out[32]) {
    out[0]=0;
    for(int i=0;i<car->count;i++) {
        long off=car->meshes[i].car_source;
        if(off<8 || off>len)continue;
        char name[64];n2_car_mesh_name(d,off,off+n2_u32(d+off-4),name);
        char *k=strstr(name,"_KIT");
        if(k && k>name && k-name<31){memcpy(out,name,(size_t)(k-name));out[k-name]=0;return;}
    }
}

static void n2_mod_option(N2PartMenu *menu,int value,const char *label) {
    for(int i=0;i<menu->count;i++)if(menu->options[i].value==value)return;
    if(menu->count>=256)return;
    N2PartOption *o=&menu->options[menu->count++];o->value=value;
    snprintf(o->label,sizeof o->label,"%s",label);
}

/* Enumerate object names, not arbitrary byte runs or guessed numeric ranges. */
typedef struct { int library; N2PartMenu *menus; } N2ModWalk;
static int n2_mod_catalog_chunk(const unsigned char *d,uint32_t tag,long p,long end,void *context) {
    N2ModWalk *walk=(N2ModWalk *)context;
    int library=walk->library;N2PartMenu *menus=walk->menus;
    long size=end-p;
    if(tag==0x80134010u) {
        char name[64],label[48];n2_car_mesh_name(d,p,p+size,name);
        int num=0;long at=0,n=0;int kind=n2_name_variant((unsigned char *)name,strlen(name),&num,&at,&n);
        int slot=n2_car_part(name);
        if(!library && slot==N2_PART_WIDE) {   /* a wide body kit: KITWnn */
            const char *kw=strstr(name,"_KITW");
            if(kw[5]>='0' && kw[5]<='9' && kw[6]>='0' && kw[6]<='9') {
                int nn=(kw[5]-'0')*10+(kw[6]-'0');
                snprintf(label,sizeof label,"Wide body %02d",nn);
                if(nn)n2_mod_option(menus+N2_PART_WIDE,1+nn,label);
            }
            return 0;
        }
        if(library)slot=n2_lib_slot(library);
        if(library>=N2_LIB_MIRROR0 && !strstr(name,"_LEFT_"))return 0;   /* one entry per style */
        /* a library's carbon fibre styles (<LIB>_STYLEnn[_DUAL|_OFFSET]_CF,
           MIRROR_<T>_LEFT_STYLEnn_CF): the same value + 500 */
        int cf=strstr(name,"_CF")!=NULL;
        if(slot>=0 && kind && (!cf || library) && !strstr(name,"WIDE") && !strstr(name,"KITW")) {
            int value=(kind-1)*100+num+1;
            N2CarConfig cfg={.body_kit=num,.hood_style=num};
            if(!library || slot==N2_PART_EXHAUST)cfg.parts[slot]=value;
            N2Scene probe={0};n2_walk_car(d,p-8,p+size,&probe,NULL,0,&cfg,0);
            int renderable=probe.count>0;n2_free_scene(&probe);
            if(!renderable)return 0; /* named empty placeholders */
            if(library>=N2_LIB_MIRROR0) {
                value=library*1000+(cf?500:0)+num;
                snprintf(label,sizeof label,"Style %02d%s",num,cf?" CF":"");
            } else if(library) {
                int variant=strstr(name,"_DUAL")?1:strstr(name,"_OFFSET")?2:0;
                value=library*1000+(cf?500:0)+variant*100+num;
                const char *type=library==1?"Coupe":library==2?"Hatch":library==3?"SUV":
                                 library==4?"Tip":variant==1?"Dual":variant==2?"Offset":"Single";
                snprintf(label,sizeof label,"Style %02d (%s%s)",num,type,cf?" CF":"");
            } else if(kind==1 && !num)snprintf(label,sizeof label,"Stock");
            else snprintf(label,sizeof label,"%s %02d",kind==1?"Kit":"Style",num);
            n2_mod_option(menus+slot,value,label);
        }
        return 0;
    }
    return (tag>>28)==8;
}
static void n2_mod_catalog_walk(const unsigned char *d,long beg,long end,
                                int library,N2PartMenu menus[N2_PART_COUNT]) {
    N2ModWalk walk={library,menus};
    asset_chunks_walk(d,beg,end,n2_mod_catalog_chunk,&walk);
}

/* A library's options do not depend on the car: walked once (its file read
   and every style probed) and kept, a few KB, instead of 4-14 MB of part
   libraries read from the card on every car switch. */
typedef struct { int n; struct { int slot, value; char label[48]; } *opt; int tried; } N2ModLibCache;
static N2ModLibCache n2_mod_lib_cache[10];
static const N2ModLibCache *n2_mod_lib_options(const char *root,int lib) {
    N2ModLibCache *c=&n2_mod_lib_cache[lib];
    if(c->tried)return c->opt?c:NULL;
    c->tried=1;
    char path[1024];long bytes=0;
    snprintf(path,sizeof path,"%s/CARS/%s/GEOMETRY.BIN",root,n2_mod_libraries[lib]);
    unsigned char *d=n2_read_file(path,&bytes);
    N2PartMenu *m=d?(N2PartMenu *)calloc(N2_PART_COUNT,sizeof *m):NULL;
    if(m) {
        n2_mod_catalog_walk(d,0,bytes,lib,m);
        int n=0;for(int s=0;s<N2_PART_COUNT;s++)n+=m[s].count;
        c->opt=calloc(n?n:1,sizeof *c->opt);
        for(int s=0;c->opt && s<N2_PART_COUNT;s++)for(int i=0;i<m[s].count;i++) {
            c->opt[c->n].slot=s;c->opt[c->n].value=m[s].options[i].value;
            memcpy(c->opt[c->n].label,m[s].options[i].label,sizeof c->opt[c->n].label);c->n++;
        }
    }
    free(m);free(d);
    return c->opt?c:NULL;
}
static void n2_mod_catalog(const char *root,const unsigned char *data,long len,
                            const N2Scene *car,N2PartMenu menus[N2_PART_COUNT]) {
    memset(menus,0,sizeof(N2PartMenu)*N2_PART_COUNT);
    for(int i=0;i<N2_PART_COUNT;i++)n2_mod_option(menus+i,0,i<N2_PART_SPOILER?"Follow kit preset":"Stock / none");
    n2_mod_catalog_walk(data,0,len,0,menus);
    char carname[32];n2_mod_car_name(data,len,car,carname);
    for(int lib=1;lib<=9;lib++) {
        float m[16];
        if(lib>=N2_LIB_MIRROR0 && (!carname[0] || lib!=n2_mirror_lib(carname)))continue;
        int supported=lib>=N2_LIB_MIRROR0?n2_car_socket(data,len,car,0x9b971511u,m)==1 &&
                                          n2_car_socket(data,len,car,0x0ee87ec4u,m)==1:
                      lib<=3?n2_car_socket(data,len,car,0xc93b73fdu,m)==1:
                      lib==5?n2_car_socket(data,len,car,0x90c81258u,m)==1:
                      n2_car_socket(data,len,car,0xbcf8a18bu,m)==1 || n2_car_socket(data,len,car,0xbd7cf15eu,m)==1;
        if(!supported)continue;
        const N2ModLibCache *c=n2_mod_lib_options(root,lib);
        for(int i=0;c && i<c->n;i++)n2_mod_option(menus+c->opt[i].slot,c->opt[i].value,c->opt[i].label);
    }
    for(int part=0;part<N2_PART_COUNT;part++) {
        N2PartMenu *m=menus+part;
        for(int i=1;i<m->count;i++) {
            N2PartOption option=m->options[i];int j=i;
            while(j && m->options[j-1].value>option.value){m->options[j]=m->options[j-1];j--;}
            m->options[j]=option;
        }
    }
}

/* Append complete material slices at the car's named socket. Spoilers/scoops
 * use source XYZ; exhaust tips use the same axis conversion as stock exhausts.
 * Painted library objects have empty texture packs and use material colours.
 * Carbon variants are excluded until their shared texture binding is supported. */
static int n2_mod_attach(const char *root,const unsigned char *data,long len,
                         int slot,int value,N2Scene *car) {
    int lib=value/1000,cf=value%1000>=500,variant=value%500/100,style=value%100;
    if(!root || lib<1 || lib>9 || !style || variant>2 || slot!=n2_lib_slot(lib) || (lib!=5 && variant))return 0;
    float sockets[2][16];int ns=0;
    int mirror=lib>=N2_LIB_MIRROR0;
    const uint32_t keys[]={mirror?0x9b971511u:slot==N2_PART_SPOILER?0xc93b73fdu:   /* MIRROR_BODY_LEFT */
                           slot==N2_PART_SCOOP?0x90c81258u:0xbcf8a18bu,
                           mirror?0x0ee87ec4u:0xbd7cf15eu};                        /* MIRROR_BODY_RIGHT */
    for(int i=0;i<(slot==N2_PART_EXHAUST || mirror?2:1);i++) {
        int found=n2_car_socket(data,len,car,keys[i],sockets[ns]);
        if(found<0)return 0;
        ns+=found;
    }
    if(!ns)return 0;
    char path[1024];long bytes=0;
    snprintf(path,sizeof path,"%s/CARS/%s/GEOMETRY.BIN",root,n2_mod_libraries[lib]);
    unsigned char *d=n2_read_file(path,&bytes);if(!d)return 0;
    N2CarConfig cfg={.hood_style=style};cfg.parts[N2_PART_EXHAUST]=101+style;
    N2Scene part={0};n2_walk_car(d,0,bytes,&part,NULL,0,&cfg,0);
    int kept=0;
    for(int i=0;i<part.count;i++) {
        N2Mesh *m=part.meshes+i;char name[64];long off=m->car_source;
        n2_car_mesh_name(d,off,off+n2_u32(d+off-4),name);
        int v=strstr(name,"_DUAL")?1:strstr(name,"_OFFSET")?2:0;
        if((strstr(name,"_CF")!=NULL)!=cf || v!=variant) {free(m->verts);free(m->idx);continue;}
        if(cf)m->texkey=N2_CF_TEX;   /* the carbon styles' own texture, CARBONFIBRE */
        /* a mirror's object: 1 left, 2 right -- each marker takes its own
           (in inst, which car meshes leave free and the LOD collapse keeps) */
        m->inst=(unsigned char)(strstr(name,"_LEFT_")?1:strstr(name,"_RIGHT_")?2:0);
        part.meshes[kept++]=*m;
    }
    part.count=kept;free(d);n2_car_dedupe_lod(&part);
    if(!part.count){n2_free_scene(&part);return 0;}
    int old=car->count,total=old+part.count*ns;
    N2Mesh *grown=(N2Mesh *)realloc(car->meshes,(size_t)total*sizeof *grown);
    if(!grown){n2_free_scene(&part);return 0;}car->meshes=grown;car->cap=total;
    for(int k=0;k<ns;k++)for(int i=0;i<part.count;i++) {
        if(mirror && part.meshes[i].inst!=k+1)continue;
        N2Mesh *src=part.meshes+i,*dst=car->meshes+car->count++;
        *dst=*src;dst->inst=0;dst->car_source=0;dst->famkey=0;dst->car_part=(unsigned char)(slot+1);
        dst->verts=(float *)malloc((size_t)src->nverts*(src->authored_normals?8:5)*sizeof(float));
        dst->idx=(uint16_t *)malloc((size_t)src->nidx*sizeof(uint16_t));dst->vcol=NULL;
        if(!dst->verts || !dst->idx){n2_free_scene(&part);return 0;}
        memcpy(dst->verts,src->verts,(size_t)src->nverts*(src->authored_normals?8:5)*sizeof(float));
        memcpy(dst->idx,src->idx,(size_t)src->nidx*sizeof(uint16_t));
        n2_car_transform(dst,sockets[k],slot==N2_PART_EXHAUST);
    }
    n2_free_scene(&part);return 1;
}

/* Trunk audio components (CarRenderInfo 0x634800, draw 0x630966): a mesh
 * at each MARKER_AUDIO_COMP_i of the fitted layout, from CARS/AUDIO. The
 * front end draws the fitted part (an unset slot: nothing), the game the
 * EMPTY cover of the slot's size; TUNED / CUSTOM layouts take the _PAINT
 * twins. n2_audio_model names a part's model (the shop's database); NULL
 * outside the front end. Names are cut at 27 characters. */
static int (*n2_audio_model)(uint32_t part,int paint,char *out,int cap);
#define N2_AUDIO_SKIN 0xa2a8f605u   /* AUDIO_SKIN */
static void n2_mod_audio(const char *root,const unsigned char *data,long len,
                         const N2CarConfig *cfg,N2Scene *car) {
    int level=cfg->parts[N2_PART_AUDIO]-1;
    if(!root || level<1)return;
    static const char *const size[3]={"SMALL","MEDIUM","LARGE"};
    static const unsigned char tab[12]={0,0,1,1,2,2,0,0,0,0,1,1};   /* 0x630c39 */
    float mk[12][16];char want[12][48];int n=0;
    uint32_t mkey[12];int found[12];
    for(int i=0;i<12;i++) {
        char key[32];snprintf(key,sizeof key,"MARKER_AUDIO_COMP_%d",i);mkey[i]=n2_bstr(key,0xffffffffu);
    }
    n2_car_sockets(data,len,car,mkey,12,mk,found);
    for(int i=0;i<12;i++) {
        want[i][0]=0;
        if(found[i]!=1)continue;
        char base[48];
        if(n2_audio_model) {if(!cfg->audio_comp[i] || !n2_audio_model(cfg->audio_comp[i],level>1,base,sizeof base))continue;}
        else snprintf(base,sizeof base,"AUDIO_COMP_EMPTY_%s%s",size[tab[i]],level>1?"_PAINT":"");
        snprintf(want[i],sizeof want[i],"%s_A",base);n++;
    }
    if(!n)return;
    char path[1024];long bytes=0;
    snprintf(path,sizeof path,"%s/CARS/AUDIO/GEOMETRY.BIN",root);
    unsigned char *d=n2_read_file(path,&bytes);if(!d)return;
    static const uint32_t keys[1]={N2_AUDIO_SKIN};
    N2CarConfig none={0};N2Scene part={0};n2_walk_car(d,0,bytes,&part,keys,1,&none,0);
    int added=0;
    for(int i=0;i<12;i++) {
        if(!want[i][0])continue;
        for(int j=0;j<part.count;j++) {
            const N2Mesh *src=part.meshes+j;char name[64];long off=src->car_source;
            if(off<8 || off>bytes)continue;
            n2_car_mesh_name(d,off,off+n2_u32(d+off-4),name);
            size_t nl2=strlen(name);
            if(nl2<20 || strncmp(want[i],name,nl2))continue;
            N2Mesh *grown=(N2Mesh *)realloc(car->meshes,(size_t)(car->count+1)*sizeof *grown);
            if(!grown)break;
            car->meshes=grown;car->cap=car->count+1;
            N2Mesh *dst=car->meshes+car->count;int fl=src->authored_normals?8:5;
            *dst=*src;dst->inst=0;dst->car_source=0;dst->famkey=0;dst->car_part=(unsigned char)(N2_PART_AUDIO+1);
            dst->car_anim=0;dst->car_mount=N2_MOUNT_BODY;dst->vcol=NULL;
            dst->verts=(float *)malloc((size_t)src->nverts*fl*sizeof(float));
            dst->idx=(uint16_t *)malloc((size_t)src->nidx*sizeof(uint16_t));
            if(!dst->verts || !dst->idx){free(dst->verts);free(dst->idx);break;}
            memcpy(dst->verts,src->verts,(size_t)src->nverts*fl*sizeof(float));
            memcpy(dst->idx,src->idx,(size_t)src->nidx*sizeof(uint16_t));
            n2_car_transform(dst,mk[i],0);
            car->count++;added++;
        }
    }
    n2_free_scene(&part);free(d);
    printf("car: trunk audio level %d: %d of %d components, %d meshes\n",level,n,12,added);
}

/* SyncVisualPartsWithPhysics 0x6399d0: FRONT_BRAKE (slot 0x1b) is the
 * BRAKES library part of the brake upgrade level (BRAKE_STYLE0n), in place
 * of the car's own front brakes; the mesh sits at the origin like those and
 * is placed per wheel by the brake mount. model: the part's model name
 * without the LOD letter (shop_upgrade_model); keys: the car's textures. */
static int n2_mod_brakes(const char *root,const char *model,const uint32_t *keys,int nkeys,N2Scene *car) {
    const char *st=model?strstr(model,"STYLE"):NULL;
    int style=st?atoi(st+5):0;
    if(!root || style<1)return 0;
    char path[1024];long bytes=0;
    snprintf(path,sizeof path,"%s/CARS/BRAKES/GEOMETRY.BIN",root);
    unsigned char *d=n2_read_file(path,&bytes);if(!d)return 0;
    N2CarConfig cfg={.hood_style=style};N2Scene part={0};
    n2_walk_car(d,0,bytes,&part,keys,nkeys,&cfg,0);
    size_t ml=strlen(model);int kept=0;
    for(int i=0;i<part.count;i++) {
        N2Mesh *m=part.meshes+i;char name[64];long off=m->car_source;
        name[0]=0;if(off>=8 && off<=bytes)n2_car_mesh_name(d,off,off+n2_u32(d+off-4),name);
        if(strncmp(name,model,ml) || (name[ml] && name[ml]!='_')){n2_mesh_geometry_release(m);continue;}
        part.meshes[kept++]=*m;
    }
    part.count=kept;free(d);n2_car_dedupe_lod(&part);
    if(!part.count){n2_free_scene(&part);printf("car: brakes %s: no meshes\n",model);return 0;}
    int n=0;
    for(int i=0;i<car->count;i++) {   /* the car's own front brakes go */
        N2Mesh *m=car->meshes+i;
        if(m->car_mount==N2_MOUNT_FRONT_BRAKE){n2_mesh_geometry_release(m);continue;}
        car->meshes[n++]=*m;
    }
    car->count=n;
    N2Mesh *grown=(N2Mesh *)realloc(car->meshes,(size_t)(n+part.count)*sizeof *grown);
    if(!grown){n2_free_scene(&part);return 0;}
    car->meshes=grown;car->cap=n+part.count;
    for(int i=0;i<part.count;i++) {
        N2Mesh *dst=car->meshes+car->count++;
        *dst=part.meshes[i];dst->inst=0;dst->car_source=0;dst->famkey=0;dst->car_part=0;
        dst->car_anim=0;dst->car_mount=N2_MOUNT_FRONT_BRAKE;dst->cat=N2_CAR_MECH;
    }
    part.count=0;n2_free_scene(&part);   /* the slices moved to the car */
    printf("car: front brakes %s: %d meshes\n",model,car->count-n);
    return 1;
}

static int g_mod_fail_part=-1,g_mod_fail_value;   /* the part n2_mod_assemble gave up on */
static int n2_mod_assemble(const char *root,const unsigned char *data,long len,
                           const N2CarConfig *cfg,N2Scene *car) {
    g_mod_fail_part=-1;
    for(int p=0;p<N2_PART_COUNT;p++) {
        int value=cfg->parts[p];if(!value)continue;
        g_mod_fail_part=p;g_mod_fail_value=value;
        if(value>=1000) {if(!n2_mod_attach(root,data,len,p,value,car))return 0;}
        else {
            if(value<1 || value>200)return 0;
            int found=0;
            for(int i=0;i<car->count;i++) {
                N2Mesh *m=car->meshes+i;
                if(m->car_part==p+1 && m->vkind==(value-1)/100+1 && m->vnum==(value-1)%100)found=1;
            }
            if(!found)return 0;
        }
    }
    g_mod_fail_part=-1;
    n2_mod_audio(root,data,len,cfg,car);
    return 1;
}
#endif
