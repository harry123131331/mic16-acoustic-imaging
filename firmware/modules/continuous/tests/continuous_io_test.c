#include "mic16_continuous.h"
#include "mic16_source_wire.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fread(void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int printf(const char *,...);
extern int strcmp(const char *,const char *);
static m16_cont_workspace workspace;
static m16_cont_prediction prediction;
static float pcm[40960*16];
typedef struct {void *audio,*wire;} outputs;
static int wire_sink(void *context,const unsigned char *data,unsigned length){
    void *out=context;
    return fwrite(&length,4,1,out)!=1 || fwrite(data,1,length,out)!=length;
}
static int emit(void *context,const m16_cont_info *info,const double *a,const double *b,unsigned n){
    outputs *out=context;
    uint32_t small[8]={info->epoch,info->window,info->source_id,info->ordinal,info->stream_count,info->current_confirmed,info->final,n};
    uint64_t big[5]={info->output_start,info->output_end,info->input_start,info->input_end,info->available_input};
    if(fwrite(small,4,8,out->audio)!=8 || fwrite(big,8,5,out->audio)!=5 ||
       fwrite(a,8,n,out->audio)!=n || fwrite(b,8,n,out->audio)!=n)return 1;
    return m16_source_encode(info,a,b,n,wire_sink,out->wire)<0;
}
static int noop(void *ctx,const m16_cont_info *i,const double *a,const double *b,unsigned n){
    (void)ctx;(void)i;(void)a;(void)b;(void)n;return 0;
}
static int fail_sink(void *ctx,const m16_cont_info *i,const double *a,const double *b,unsigned n){
    (void)ctx;(void)i;(void)a;(void)b;(void)n;return 1;
}
static int empty_packet(void *ctx,const unsigned char *data,unsigned n){
    unsigned *count=ctx;
    if(n!=60 || data[0]!='M' || data[6]!=6 || data[16] || data[22] || data[52])return -1;
    (*count)++;return 0;
}
static int failed_packet(void *ctx,const unsigned char *data,unsigned n){
    (void)ctx;(void)data;(void)n;return 1;
}
static int selftest(void){
    unsigned checks=0,errors=0,i;union{uint32_t u;float f;}nan;
    m16_cont_info info={0,0,0,0,0,0,0,0,0,0,0,0};unsigned packets=0;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("FAILED_CHECK %u\n",checks);}}while(0)
    CHECK(m16_cont_init(0,1)<0);CHECK(m16_cont_init(&workspace,0)<0);
    CHECK(!m16_cont_init(&workspace,1));
    prediction.count=9;CHECK(m16_cont_push(&workspace,pcm,&prediction,0,noop,0)<0 && workspace.failed);
    prediction.count=0;CHECK(m16_cont_push(&workspace,pcm,&prediction,0,noop,0)<0);
    m16_cont_init(&workspace,2);CHECK(m16_cont_push(&workspace,pcm,&prediction,1,noop,0)<0 && !workspace.windows);
    m16_cont_init(&workspace,3);nan.u=0x7fc00000;pcm[0]=nan.f;
    CHECK(m16_cont_push(&workspace,pcm,&prediction,0,noop,0)<0);pcm[0]=0.;
    m16_cont_init(&workspace,4);prediction.count=2;prediction.ids[0]=prediction.ids[1]=1;
    CHECK(m16_cont_push(&workspace,pcm,&prediction,0,noop,0)<0);
    prediction.count=1;for(i=0;i<320;i++)prediction.weights[i]=1.f/16.f;
    for(i=0;i<67*64;i++)prediction.masks[0][i]=1.f;
    m16_cont_init(&workspace,5);prediction.masks[0][0]=nan.f;
    CHECK(m16_cont_push(&workspace,pcm,&prediction,0,noop,0)<0);prediction.masks[0][0]=1.f;
    m16_cont_init(&workspace,6);prediction.weights[0]=1.f;
    CHECK(m16_cont_push(&workspace,pcm,&prediction,0,noop,0)<0);prediction.weights[0]=1.f/16.f;
    m16_cont_init(&workspace,7);CHECK(m16_cont_push(&workspace,workspace.pcm[0],&prediction,0,noop,0)<0);
    m16_cont_init(&workspace,8);prediction.count=0;
    CHECK(m16_cont_push(&workspace,pcm,&prediction,0,noop,0)==0 && !workspace.emitted);
    CHECK(m16_cont_push(&workspace,pcm,&prediction,40960,noop,0)==0 && workspace.emitted==1);
    CHECK(m16_cont_finish(&workspace,noop,0)==0 && workspace.emitted==2 && workspace.closed);
    CHECK(m16_cont_finish(&workspace,noop,0)<0);
    CHECK(m16_cont_push(&workspace,pcm,&prediction,81920,noop,0)<0);
    m16_cont_init(&workspace,9);m16_cont_push(&workspace,pcm,&prediction,0,noop,0);
    CHECK(m16_cont_finish(&workspace,fail_sink,0)<0 && workspace.failed && !workspace.emitted);
    info.epoch=1;info.output_end=16778;info.final=1;
    CHECK(m16_source_encode(&info,0,0,16778,empty_packet,&packets)==1 && packets==1);
    CHECK(m16_source_encode(&info,0,0,16778,failed_packet,0)==-2);
    info.output_end=16777;CHECK(m16_source_encode(&info,0,0,16778,empty_packet,&packets)<0);
    info.output_end=16778;info.source_id=1;info.stream_count=1;
    CHECK(m16_source_encode(&info,0,0,16778,empty_packet,&packets)<0);
    printf("CONTINUOUS_SELFTEST checks=%u errors=%u workspace_bytes=%u prediction_bytes=%u\n",checks,errors,
        (unsigned)sizeof(workspace),(unsigned)sizeof(prediction));return (int)errors;
}
int main(int argc,char **argv){
    void *in;outputs out;uint32_t windows,c,i;int code;
    if(argc==2 && !strcmp(argv[1],"selftest"))return selftest();
    if(argc!=4)return 9;
    in=fopen(argv[1],"rb");out.audio=fopen(argv[2],"wb");out.wire=fopen(argv[3],"wb");
    if(!in || !out.audio || !out.wire)return 8;
    if(fread(&windows,4,1,in)!=1 || !windows || windows>4)return 7;
    if(m16_cont_init(&workspace,20261019u))return 6;
    for(c=0;c<windows;c++){
        if(fread(pcm,4,40960*16,in)!=40960*16 || fread(&prediction.count,4,1,in)!=1 || prediction.count>8)return 7;
        if(fread(prediction.ids,4,prediction.count,in)!=prediction.count ||
           fread(prediction.directions,16,prediction.count,in)!=prediction.count || fread(prediction.weights,4,320,in)!=320)return 7;
        for(i=0;i<prediction.count;i++)if(fread(prediction.masks[i],4,67*64,in)!=67*64)return 7;
        code=m16_cont_push(&workspace,pcm,&prediction,(uint64_t)c*40960,emit,&out);
        if(code<0){printf("PUSH_ERROR cycle=%u\n",c);return 5;}
    }
    code=m16_cont_finish(&workspace,emit,&out);if(code<0)return 5;
    if(fclose(in) || fclose(out.audio) || fclose(out.wire))return 7;
    printf("CONTINUOUS_IO windows=%u emitted=%u failed=%u\n",windows,workspace.emitted,workspace.failed);return 0;
}
