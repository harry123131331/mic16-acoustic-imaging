#include "../../mic16_cooperate.h"
#include "mic16_beam_index.h"
extern double ceil(double);
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
 __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
 return x<=y?y-x<na:x-y<nb;
}
static float mean16(const float *a){
 float r[8],s;int i;
 for(i=0;i<8;i++)r[i]=a[i]+a[i+8];
 s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
 return s/16.f;
}
int mic16_candidate_beam_indexed(const float *x,unsigned int n,const double *d,float *beam,unsigned int *guard){
 float arrival[16],lags[32],aligned[16];double minshift=0.;unsigned int i,c,g;
 if(!x || !beam || !guard || (n!=8192 && n!=40960) || mic16_candidate_geometry(d,arrival,lags))return -1;
 if(overlap(x,(unsigned long)n*16*4,beam,(unsigned long)n*4))return -1;
 for(i=0;i<n*16;i++)if(!(x[i]>=-1.f && x[i]<=1.f))return -1;
 for(c=0;c<16;c++){double shift=(double)arrival[c]-10.;if(shift>0.)return -1;if(shift<minshift)minshift=shift;}
 g=(unsigned int)ceil(-minshift)+2;
 for(i=0;i<n;i++){
  if((i&127u)==0)M16_COOPERATE();
  if(i<g){beam[i]=0.f;continue;}
  for(c=0;c<16;c++){
   /* i>=ceil(-minshift)+2且shift<=0，故2<=q<=i<n<=40960。
    * 非负有限q转无符号整数与floor(q)相同；q与fraction仍逐样本按原式计算。 */
   double q=(double)i+((double)arrival[c]-10.);unsigned int lo=(unsigned int)q;
   double fraction=q-(double)lo;
   double y=(double)x[lo*16+c];
   if(fraction!=0. && lo+1<n)y+=((double)x[(lo+1)*16+c]-(double)x[lo*16+c])*fraction;
   aligned[c]=(float)y;
  }
  beam[i]=mean16(aligned);
 }
 *guard=g;return 0;
}
