/* lod_census -- which car parts exist at which LOD tier (A..D).
 * Retail (CarRenderInfo::Render 0x620ae0) draws slot[part*4+lod] and skips a
 * part with no model at that lod. This lists every tier the walk finds before
 * n2_car_dedupe_lod, with its suffix letter and whether dedupe keeps it.
 *   lod_census DATAROOT CAR [CAR...]
 */
#include "../src/car_mod.h"
#include <stdio.h>

static char suffix(const unsigned char *d,long len,const N2Mesh *m){
    char nm[64];n2_car_source_name(d,len,m,nm);
    size_t L=strlen(nm);
    return (L>=2&&nm[L-2]=='_'&&nm[L-1]>='A'&&nm[L-1]<='D')?nm[L-1]:'-';
}
int main(int argc,char **argv){
    if(argc<3){fprintf(stderr,"usage: %s DATAROOT CAR...\n",argv[0]);return 2;}
    static const N2CarConfig stock={0};
    for(int a=2;a<argc;a++){
        char p[1024];snprintf(p,sizeof p,"%s/CARS/%s/GEOMETRY.BIN",argv[1],argv[a]);
        long len=0;unsigned char *d=n2_read_file(p,&len);
        if(!d){printf("%s: unreadable\n",argv[a]);continue;}
        N2Scene s;memset(&s,0,sizeof s);
        n2_walk_car(d,0,len,&s,NULL,0,&stock,0);
        n2_car_apply_config(&s,&stock);
        int n=s.count;
        uint32_t *tid=malloc(n*4);for(int i=0;i<n;i++)tid[i]=s.meshes[i].tierid;
        N2Scene k=s;  /* dedupe frees dropped meshes' arrays; keep ids first */
        uint32_t *ktid=NULL;int nk=0;
        {
            N2Scene c;memset(&c,0,sizeof c);n2_walk_car(d,0,len,&c,NULL,0,&stock,0);
            n2_car_apply_config(&c,&stock);n2_car_dedupe_lod(&c);
            nk=c.count;ktid=malloc((nk?nk:1)*4);for(int i=0;i<nk;i++)ktid[i]=c.meshes[i].tierid;
            int at[4]={0},w4[4]={0},always=0;
            for(int i=0;i<nk;i++){
                unsigned b=c.meshes[i].lodbits;if(!b){always++;b=15;}
                int mult=c.meshes[i].car_mount?4:1;   /* wheels x4; brakes x2 per mount, close enough */
                if(c.meshes[i].car_mount==2||c.meshes[i].car_mount==3)mult=2;
                for(int L=0;L<4;L++)if(b&(1u<<L)){at[L]++;w4[L]+=mult;}
                if(getenv("K")){char nm[64];n2_car_source_name(d,len,&c.meshes[i],nm);
                    printf("  kept bits=%x mount=%d mat=%08x %s\n",c.meshes[i].lodbits,c.meshes[i].car_mount,c.meshes[i].car_material,nm);}
            }
            printf("  slices at LOD0..3: %d %d %d %d (draw-weighted %d %d %d %d), %d unlettered/unknown (all LODs)\n",
                   at[0],at[1],at[2],at[3],w4[0],w4[1],w4[2],w4[3],always);
            /* tierids are a global counter: re-walk renumbers, so offset */
        }
        (void)k;
        /* per letter: meshes, kept */
        int cnt[5]={0},idx[5]={0};
        printf("== %s: %d meshes walked, %d kept by dedupe\n",argv[a],n,nk);
        for(int i=0;i<n;i++){
            char nm[64];n2_car_source_name(d,len,&s.meshes[i],nm);
            char c=suffix(d,len,&s.meshes[i]);int L=c=='-'?4:c-'A';
            cnt[L]++;idx[L]+=s.meshes[i].nidx;
            if(getenv("V"))printf("  %c tier=%u mount=%d cat=%d tex=%08x mat=%08x nidx=%d %s\n",c,tid[i],
                s.meshes[i].car_mount,s.meshes[i].cat,s.meshes[i].texkey,s.meshes[i].car_material,s.meshes[i].nidx,nm);
        }
        for(int L=0;L<5;L++)printf("  %c: %3d meshes %7d idx\n","ABCD-"[L],cnt[L],idx[L]);
        free(tid);free(ktid);
    }
    return 0;
}
