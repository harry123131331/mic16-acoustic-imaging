#ifndef MIC16_FFT_PROVIDER_H
#define MIC16_FFT_PROVIDER_H
/* 由常驻调度器保证服务进展；取消后必须持续驱动后端，直到owns为0。
 * request返回0已登记、1设备忙、2安全回CPU、负数拒绝；poll为0完成/1进行中/2回CPU。
 * 返回2时客户端已不持有后端资源，原输入未被改写，必须从原CPU位倒序开始继续。
 * 客户端对象及输入数组不能在owns期间释放，不能清对象绕过超时隔离。 */
typedef struct {
 void *context;
 int (*request)(void *,const void *,double *,double *,unsigned,const double *,const double *,unsigned,unsigned);
 int (*poll)(void *,const void *);
 int (*cancel)(void *,const void *);
 int (*owns)(const void *,const void *);
} m16_fft_provider;
#endif
