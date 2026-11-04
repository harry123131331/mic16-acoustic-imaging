#include "mic16_directions_wire.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int printf(const char *,...);
extern double sin(double);
static double a[16778],b[16778];
static mic16_cycle_output event;
static int output(void *context,const unsigned char *bytes,unsigned n){void *f=context;return fwrite(&n,4,1,f)!=1 || fwrite(bytes,1,n,f)!=n;}
static int accepted(void *ctx,const unsigned char *b,unsigned n){(void)ctx;(void)b;(void)n;return 0;}
int main(int argc,char **argv){
    void *f;unsigned window,id,i;int checks=0,errors=0;
    m16_cont_info info={0,0,0,0,0,0,0,0,0,0,0,0};
    if(argc!=2)return 9;f=fopen(argv[1],"wb");if(!f)return 8;
    checks++;if(m16_directions_encode(0,0,&event,accepted,0)>=0)errors++;
    event.count=2;event.tracks[0].source_id=11;event.tracks[1].source_id=11;
    checks++;if(m16_directions_encode(777,0,&event,accepted,0)>=0)errors++;
    event.count=1;event.confirmed_count=1;event.tracks[0].confirmed_this_cycle=1;
    event.tracks[0].score=.64;checks++;if(m16_directions_encode(777,0,&event,accepted,0)>=0)errors++;
    event.tracks[0].score=.9;event.tracks[0].azimuth=-30;event.tracks[0].elevation=5;
    for(window=0;window<2;window++){
        event.count=event.confirmed_count=(int)window+1;
        event.tracks[1].source_id=22;event.tracks[1].score=.8;event.tracks[1].azimuth=35;
        event.tracks[1].elevation=8;event.tracks[1].confirmed_this_cycle=1;
        if(m16_directions_encode(777,window,&event,output,f)<0)return 7;
        for(id=0;id<2;id++){
            unsigned samples=window?16777:16778;
            info.epoch=777;info.window=window;info.source_id=id?22:11;info.ordinal=id;info.stream_count=2;
            info.current_confirmed=window || !id;info.final=window;
            info.output_start=window?16778:0;info.output_end=window?33555:16778;
            info.input_start=(uint64_t)window*40960;info.input_end=((uint64_t)window+1)*40960;info.available_input=81920;
            for(i=0;i<samples;i++){a[i]=.05*(id+1)*sin((double)(info.output_start+i)*.03);b[i]=.9*a[i];}
            if(m16_source_encode(&info,a,b,samples,output,f)<0)return 6;
        }
    }
    if(fclose(f))return 5;
    printf("DIRECTION_ENCODER checks=%d errors=%d synthetic_windows=2 no_acoustic_metrics\n",checks,errors);
    return errors;
}
