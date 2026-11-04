// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_H
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#ifndef __linux__
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xmic16_tf_zone_streaming_hls_q2_24_hw.h"

/**************************** Type Definitions ******************************/
#ifdef __linux__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef struct {
    u16 DeviceId;
    u32 Control_BaseAddress;
} XMic16_tf_zone_streaming_hls_q2_24_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XMic16_tf_zone_streaming_hls_q2_24;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XMic16_tf_zone_streaming_hls_q2_24_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XMic16_tf_zone_streaming_hls_q2_24_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XMic16_tf_zone_streaming_hls_q2_24_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XMic16_tf_zone_streaming_hls_q2_24_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#ifndef __linux__
int XMic16_tf_zone_streaming_hls_q2_24_Initialize(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u16 DeviceId);
XMic16_tf_zone_streaming_hls_q2_24_Config* XMic16_tf_zone_streaming_hls_q2_24_LookupConfig(u16 DeviceId);
int XMic16_tf_zone_streaming_hls_q2_24_CfgInitialize(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, XMic16_tf_zone_streaming_hls_q2_24_Config *ConfigPtr);
#else
int XMic16_tf_zone_streaming_hls_q2_24_Initialize(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, const char* InstanceName);
int XMic16_tf_zone_streaming_hls_q2_24_Release(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
#endif

void XMic16_tf_zone_streaming_hls_q2_24_Start(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
u32 XMic16_tf_zone_streaming_hls_q2_24_IsDone(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
u32 XMic16_tf_zone_streaming_hls_q2_24_IsIdle(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
u32 XMic16_tf_zone_streaming_hls_q2_24_IsReady(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_EnableAutoRestart(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_DisableAutoRestart(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);

void XMic16_tf_zone_streaming_hls_q2_24_Set_ddr_arena(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u64 Data);
u64 XMic16_tf_zone_streaming_hls_q2_24_Get_ddr_arena(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_Set_observation_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data);
u32 XMic16_tf_zone_streaming_hls_q2_24_Get_observation_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_Set_initial_phasor_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data);
u32 XMic16_tf_zone_streaming_hls_q2_24_Get_initial_phasor_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_Set_bin_rotator_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data);
u32 XMic16_tf_zone_streaming_hls_q2_24_Get_bin_rotator_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_Set_output_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data);
u32 XMic16_tf_zone_streaming_hls_q2_24_Get_output_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);

void XMic16_tf_zone_streaming_hls_q2_24_InterruptGlobalEnable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_InterruptGlobalDisable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
void XMic16_tf_zone_streaming_hls_q2_24_InterruptEnable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Mask);
void XMic16_tf_zone_streaming_hls_q2_24_InterruptDisable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Mask);
void XMic16_tf_zone_streaming_hls_q2_24_InterruptClear(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Mask);
u32 XMic16_tf_zone_streaming_hls_q2_24_InterruptGetEnabled(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);
u32 XMic16_tf_zone_streaming_hls_q2_24_InterruptGetStatus(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
