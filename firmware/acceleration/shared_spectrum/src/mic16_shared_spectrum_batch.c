#include "mic16_shared_spectrum_batch.h"
#include "mic16_cooperate.h"
extern double log10(double);
static double sum(const double *a,int n){
    int i,j;double s=-0.;
    if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
    if(n<=128){double r[8];for(j=0;j<8;j++)r[j]=a[j];
        for(i=8;i<n-n%8;i+=8)for(j=0;j<8;j++)r[j]+=a[i+j];
        s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
        for(;i<n;i++)s+=a[i];return s;}
    i=n/2/8*8;return sum(a,i)+sum(a+i,n-i);
}
static const unsigned m16_band_start[32]={0,25,50,76,101,126,151,177,202,227,252,278,303,328,353,378,404,429,454,479,505,530,555,580,606,631,656,681,706,732,757,782};
static const unsigned m16_band_count[32]={25,25,26,25,25,25,26,25,25,25,26,25,25,25,25,26,25,25,25,26,25,25,25,26,25,25,25,25,26,25,25,25};

int m16_shared_mono_bands(const float *beam,unsigned frames,double *bands,double *levels,
 mic16_physical_workspace *work,uint64_t *arena,size_t words,m16_shared_spectrum_execute execute,void *context){
 unsigned i,base,c,active,t,k,b;mic16_spectral_workspace *w;
 if(!beam || !bands || !levels || !work || !arena || !execute ||
    (frames!=4 && frames!=20) || words<M16_SHARED_ARENA_WORDS || ((uintptr_t)arena&63u))return -1;
 w=&work->spectral;if(w->initialized!=0x4d313653u)return -1;
 for(i=0;i<frames*2048;i++)if(!(beam[i]>=-1.f && beam[i]<=1.f))return -1;
 for(base=0;base<frames;base+=active){
  active=frames-base;if(active>16)active=16;
  for(c=0;c<active;c++){
   double mean,energy;int rc;t=base+c;M16_COOPERATE();
   for(i=0;i<2048;i++){double x=beam[t*2048+i];w->fft_re[i]=x;work->scratch[i]=x*x;}
   mean=sum(w->fft_re,2048)/2048.;energy=sum(work->scratch,2048)/2048.;
   levels[t]=10.*log10(energy>1e-20?energy:1e-20);
   for(i=0;i<2048;i++)w->fft_re[i]=(w->fft_re[i]-mean)*w->hann[i];
   rc=m16_shared_pack_contiguous(w->fft_re,c,arena,words);
   if(rc)return rc==1?1:-1;
  }
  if(execute(context,arena,active))return -2;
  for(c=0;c<active;c++){
   t=base+c;M16_COOPERATE();
   if(m16_shared_read_bins(arena,words,c,w->fft_re+32,w->fft_im+32))return -2;
   for(b=0;b<807;b++){double re=w->fft_re[b+32],im=w->fft_im[b+32];work->powers[b]=re*re+im*im;}
   for(k=0;k<32;k++){
    unsigned start=m16_band_start[k],count=m16_band_count[k];double power=sum(work->powers+start,count)/count;
    bands[t*32+k]=10.*log10(power>1e-20?power:1e-20);
   }
  }
 }
 return 0;
}
