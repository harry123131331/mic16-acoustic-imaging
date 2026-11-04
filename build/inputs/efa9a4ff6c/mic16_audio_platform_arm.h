#ifndef MIC16_AUDIO_PLATFORM_ARM_H
#define MIC16_AUDIO_PLATFORM_ARM_H
#include "mic16_audio_resident_task.h"
#include "mic16_window_capture_arm.h"
#include "mic16_control_udp.h"
typedef struct {
 unsigned initialized,ready;
 int error;
 uint32_t boot;
 const float *reference;
 const char *key;
 m16_window_capture capture;
 m16_window_capture_arm capture_port;
 mic16_stream_scheduler scheduler;
 m16_capture_stream capture_stream;
 m16_shared_spectrum_task fft_task;
 m16_projection_runtime projection;
 m16_detection_task detection;
 m16_enh_prediction_task enhancement;
 m16_continuous_task continuous;
 m16_source_audio_task source_audio;
 m16_audio_pipeline_task pipeline;
 m16_output_pool output;
 m16_resident_control control;
 m16_control_udp control_udp;
 m16_audio_resident_task resident;
} m16_audio_platform_arm;
/* 只用于匹配的纯音频BIT/XSA。网络已初始化且DMA未启动。
 * cfg的三个storage/physical由本平台绑定，调用者仅提供数字格式/预热/期限。
 * 参考必须是明确配置的独立8192×16 F32缓存，不在启动时采背景。 */
int m16_audio_platform_arm_init(m16_audio_platform_arm *,const m16_window_capture_config *,
 const float *reference,const char *key,uint32_t boot);
/* 初始化相位表按步推进，返回0准备完毕、1进行中、负数失败；期间主循环继续服务网络。
 * 准备完毕后只转发常驻step，没有有限窗上限；控制UDP由主循环独立按配额推进。 */
int m16_audio_platform_arm_step(m16_audio_platform_arm *);
#endif
