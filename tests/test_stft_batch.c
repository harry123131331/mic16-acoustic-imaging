/* 批次借用边界、逐帧FFT状态、错误不改输出、跨调用重新校验及回调中止。 */
#include "mic16_stft_batch.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
static mic16_extended_workspace batch,reference;
static double original[16778];
static unsigned checks,errors,cases,frames_checked;
typedef struct {unsigned n,next,stop;} check_context;
static void require(int ok){checks++;if(!ok)errors++;}
static int compare_frame(unsigned t,const double *s,void *context){
    check_context *v=(check_context *)context;
    require(t==v->next && s==batch.spectrum);v->next++;
    require(mic16_stft512_frame(reference.resampled,v->n,t,&reference)==0);
    require(!memcmp(s,reference.spectrum,sizeof(reference.spectrum)));
    require(!memcmp(batch.fft_re,reference.fft_re,sizeof(reference.fft_re)));
    require(!memcmp(batch.fft_im,reference.fft_im,sizeof(reference.fft_im)));
    frames_checked++;
    return v->stop && v->next==v->stop?-7:0;
}
static int forbidden(unsigned t,const double *s,void *context){(void)t;(void)s;(void)context;require(0);return 0;}
static unsigned state=20261020u;
static unsigned random32(void){state=state*1664525u+1013904223u;return state;}
static void fill(unsigned n,unsigned pattern){
    unsigned i;
    for(i=0;i<n;i++){
        double x=0.;
        if(pattern==0)x=(double)((int)(random32()%65537)-32768)/32768.;
        if(pattern==1)x=i%2?-1.:1.;
        if(pattern==2)x=i%2?-0.:0.;
        if(pattern==3)x=i%2?-0x1p-1074:0x1p-1074;
        if(pattern==4)x=i==0 || i==n-1 || i==255 || i==256 || i==511?1.:0.;
        if(pattern==5)x=i%2?-1.7976931348623157e308:1.7976931348623157e308;
        batch.resampled[i]=reference.resampled[i]=original[i]=x;
    }
}
static void rejected(const double *x,unsigned n,unsigned frames,mic16_extended_workspace *w,mic16_stft_consumer consume){
    double snapshot[512*2+514];
    memcpy(snapshot,batch.fft_re,sizeof(snapshot));
    require(mic16_stft512_visit(x,n,frames,w,consume,0)==-1);
    require(!memcmp(snapshot,batch.fft_re,sizeof(snapshot)));cases++;
}
int main(void){
    const unsigned sizes[]={512,513,768,3356,8192,16778};unsigned k,p,i,j;check_context context;
    union {unsigned long long u;double d;} bad;
    memset(&batch,0xa5,sizeof(batch));memset(&reference,0xa5,sizeof(reference));
    for(k=0;k<6;k++)for(p=0;p<6;p++){
        unsigned n=sizes[k],frames=(n+255)/256+1;fill(n,p);context.n=n;context.next=context.stop=0;
        require(!mic16_stft512_visit(batch.resampled,n,frames,&batch,compare_frame,&context));
        require(context.next==frames);require(!memcmp(original,batch.resampled,n*sizeof(double)));cases++;
    }
    fill(16778,0);
    rejected(0,16778,67,&batch,forbidden);
    rejected(batch.resampled+1,16778,67,&batch,forbidden);
    rejected(reference.resampled,16778,67,&batch,forbidden);
    rejected(batch.resampled,511,3,&batch,forbidden);
    rejected(batch.resampled,16779,67,&batch,forbidden);
    rejected(batch.resampled,16778,0,&batch,forbidden);
    rejected(batch.resampled,16778,68,&batch,forbidden);
    rejected(batch.resampled,16778,67,0,forbidden);
    rejected(batch.resampled,16778,67,&batch,0);
    for(k=0;k<3;k++)for(p=0;p<3;p++){
        unsigned index=p==0?0:p==1?8389:16777;
        bad.u=k==0?0x7ff8000000000001ull:k==1?0x7ff0000000000000ull:0xfff0000000000000ull;
        batch.resampled[index]=bad.d;
        /* 即使只请求第一帧，也须先拒绝整段后部非法值，与公开入口一致。 */
        rejected(batch.resampled,16778,1,&batch,forbidden);batch.resampled[index]=original[index];
    }
    for(j=0;j<2;j++){
        fill(3356,j);context.n=3356;context.next=0;context.stop=j?4:1;
        require(mic16_stft512_visit(batch.resampled,3356,15,&batch,compare_frame,&context)==-7);
        require(context.next==context.stop);cases++;
    }
    /* 同一地址下次调用必须重验；不能把上次成功当作永久令牌。 */
    fill(3356,0);context.n=3356;context.next=context.stop=0;
    require(!mic16_stft512_visit(batch.resampled,3356,15,&batch,compare_frame,&context));
    bad.u=0x7ff0000000000000ull;batch.resampled[3355]=bad.d;
    rejected(batch.resampled,3356,15,&batch,forbidden);
    fill(3356,1);context.next=0;
    require(!mic16_stft512_visit(batch.resampled,3356,15,&batch,compare_frame,&context));
    /* 保留公开入口仍接受独立数组，并仍拒绝尾部非法值。 */
    require(!mic16_stft512_frame(original,3356,0,&reference));
    original[3355]=bad.d;require(mic16_stft512_frame(original,3356,0,&reference)==-1);
    for(i=0;i<3;i++)require(((unsigned char *)batch.observed)[i]==0xa5);
    printf("STFT_BATCH_%s checks=%u cases=%u frames=%u errors=%u\n",errors?"FAIL":"PASS",checks,cases,frames_checked,errors);
    return errors?1:0;
}
