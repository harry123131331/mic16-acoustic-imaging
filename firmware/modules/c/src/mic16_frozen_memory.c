#include "mic16_frozen.h"
extern double sin(double);
extern double cos(double);
extern double acos(double);
static int finite_local(double x) { return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308; }
static double abs_local(double x) { return x<0.0 ? -x : x; }
static void clear_bytes(void *p,unsigned int n) { unsigned char *b=(unsigned char *)p; unsigned int i; for(i=0;i<n;i++) b[i]=0; }
static int key_length(const char *s) { int i; if(!s)return -1; for(i=0;i<MIC16_POSE_KEY_BYTES;i++)if(!s[i])return i; return -1; }
static int same_key(const char *a,const char *b) { int i=0; while(a[i] && a[i]==b[i])i++; return a[i]==b[i]; }
static double angle(double a0,double e0,double a1,double e1) {
    const double r=0.017453292519943295;
    double ax=a0*r,ae=e0*r,bx=a1*r,be=e1*r;
    double au[3],bu[3],dot;
    au[0]=cos(ae)*sin(ax); au[1]=sin(ae); au[2]=cos(ae)*cos(ax);
    bu[0]=cos(be)*sin(bx); bu[1]=sin(be); bu[2]=cos(be)*cos(bx);
    dot=au[0]*bu[0]+au[1]*bu[1]+au[2]*bu[2];
    if(dot>1.0)dot=1.0; if(dot< -1.0)dot=-1.0;
    return acos(dot)/r;
}
void mic16_memory_init(mic16_search_memory *m) {
    if(!m)return;
    clear_bytes(m,(unsigned int)sizeof(*m)); m->cycle=-1; m->next_id=1;
}
typedef struct { double az,el,score; } observation;
typedef struct { double distance; int ti,oi; } edge;
static int before(edge a,edge b) {
    if(a.distance!=b.distance)return a.distance<b.distance;
    if(a.ti!=b.ti)return a.ti<b.ti;
    return a.oi<b.oi;
}
int mic16_consume_scored_cycle(mic16_search_memory *m,const double *d,
    const unsigned char *valid,const double *scores,double time,
    const char *key,int contiguous,mic16_cycle_output *output) {
    mic16_search_memory next;
    mic16_cycle_output out;
    observation obs[16]; edge edges[128];
    int c,j,i,n=0,ne=0,keylen,changed,best=-1,used_t[8]={0},used_o[16]={0};
    if(!m || !d || !valid || !scores || !output)return -1;
    keylen=key_length(key);
    if(keylen<=0 || !finite_local(time) || (contiguous!=0 && contiguous!=1) || m->count<0 || m->count>8)return -1;
    for(c=0;c<16;c++) {
        if(!finite_local(d[c*2]) || !finite_local(d[c*2+1]) || !finite_local(scores[c]) || valid[c]>1 || scores[c]<0.0 || scores[c]>1.0)return -1;
    }
    if(m->has_previous_cycle && time<=m->last_audio_end)return -2;
    next=*m; clear_bytes(&out,(unsigned int)sizeof(out));
    changed=next.has_previous_cycle && !same_key(next.pose_key,key);
    if(changed || !contiguous)next.count=0;
    next.cycle++; next.last_audio_end=time; next.has_previous_cycle=1;
    for(i=0;i<=keylen;i++)next.pose_key[i]=key[i];
    out.cycle=next.cycle;out.pose_reset=changed;
    if(!contiguous) { out.gap_reset=1;*m=next;*output=out;return 0; }
    for(c=0;c<16;c++) {
        double a=d[c*2],e=d[c*2+1];
        if(!valid[c] || scores[c]<0.65 || abs_local(a)>60.0+1e-9 || abs_local(e)>30.0+1e-9)continue;
        obs[n].az=a< -60.0 ? -60.0 : (a>60.0 ? 60.0 : a);
        obs[n].el=e< -30.0 ? -30.0 : (e>30.0 ? 30.0 : e);
        obs[n++].score=scores[c];
    }
    for(i=0;i<next.count;i++) for(j=0;j<n;j++) {
        edge x;x.distance=angle(next.tracks[i].azimuth,next.tracks[i].elevation,obs[j].az,obs[j].el);x.ti=i;x.oi=j;
        c=ne++;
        while(c>0 && before(x,edges[c-1])) { edges[c]=edges[c-1];c--; }
        edges[c]=x;
    }
    for(c=0;c<ne;c++) {
        int ti=edges[c].ti,oi=edges[c].oi;
        /* 此处精确15度匹配原记忆刷新算子；新源及邻域算子另行使用已声明的1e-9容差。 */
        if(edges[c].distance<=15.0 && !used_t[ti] && !used_o[oi]) {
            next.tracks[ti].azimuth=obs[oi].az; next.tracks[ti].elevation=obs[oi].el;
            next.tracks[ti].score=obs[oi].score; next.tracks[ti].last_seen_cycle=next.cycle;
            used_t[ti]=1;used_o[oi]=1;
        }
    }
    j=0;
    for(i=0;i<next.count;i++) {
        if(next.cycle-next.tracks[i].last_seen_cycle>=3)out.expired_ids[out.expired_count++]=next.tracks[i].source_id;
        else next.tracks[j++]=next.tracks[i];
    }
    next.count=j;
    for(i=0;i<n;i++) {
        int novel=1;
        for(j=0;j<next.count;j++)if(angle(obs[i].az,obs[i].el,next.tracks[j].azimuth,next.tracks[j].elevation)<=15.0+1e-9) { novel=0;break; }
        if(!novel)continue;
        if(best<0 || obs[i].score>obs[best].score ||
           (obs[i].score==obs[best].score && (obs[i].az<obs[best].az ||
            (obs[i].az==obs[best].az && obs[i].el<obs[best].el))))best=i;
    }
    if(best>=0 && next.count<8) {
        mic16_track t;clear_bytes(&t,(unsigned int)sizeof(t));
        t.source_id=next.next_id++;t.azimuth=obs[best].az;t.elevation=obs[best].el;t.score=obs[best].score;
        t.first_seen_cycle=t.last_seen_cycle=next.cycle;t.confirmed_this_cycle=1;
        next.tracks[next.count++]=t;out.new_source=t;out.new_source_valid=1;
    }
    out.count=next.count;
    for(i=0;i<next.count;i++) {
        next.tracks[i].confirmed_this_cycle=next.tracks[i].last_seen_cycle==next.cycle;
        out.tracks[i]=next.tracks[i];out.confirmed_count+=out.tracks[i].confirmed_this_cycle;
    }
    *m=next;*output=out;return 0;
}
