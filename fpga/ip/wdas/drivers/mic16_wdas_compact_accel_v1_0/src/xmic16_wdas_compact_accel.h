// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef XMIC16_WDAS_COMPACT_ACCEL_H
#define XMIC16_WDAS_COMPACT_ACCEL_H

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
#include "xmic16_wdas_compact_accel_hw.h"

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
} XMic16_wdas_compact_accel_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XMic16_wdas_compact_accel;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XMic16_wdas_compact_accel_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XMic16_wdas_compact_accel_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XMic16_wdas_compact_accel_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XMic16_wdas_compact_accel_ReadReg(BaseAddress, RegOffset) \
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
int XMic16_wdas_compact_accel_Initialize(XMic16_wdas_compact_accel *InstancePtr, u16 DeviceId);
XMic16_wdas_compact_accel_Config* XMic16_wdas_compact_accel_LookupConfig(u16 DeviceId);
int XMic16_wdas_compact_accel_CfgInitialize(XMic16_wdas_compact_accel *InstancePtr, XMic16_wdas_compact_accel_Config *ConfigPtr);
#else
int XMic16_wdas_compact_accel_Initialize(XMic16_wdas_compact_accel *InstancePtr, const char* InstanceName);
int XMic16_wdas_compact_accel_Release(XMic16_wdas_compact_accel *InstancePtr);
#endif

void XMic16_wdas_compact_accel_Start(XMic16_wdas_compact_accel *InstancePtr);
u32 XMic16_wdas_compact_accel_IsDone(XMic16_wdas_compact_accel *InstancePtr);
u32 XMic16_wdas_compact_accel_IsIdle(XMic16_wdas_compact_accel *InstancePtr);
u32 XMic16_wdas_compact_accel_IsReady(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_EnableAutoRestart(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_DisableAutoRestart(XMic16_wdas_compact_accel *InstancePtr);

void XMic16_wdas_compact_accel_Set_pcm(XMic16_wdas_compact_accel *InstancePtr, u64 Data);
u64 XMic16_wdas_compact_accel_Get_pcm(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_Set_integer_delay(XMic16_wdas_compact_accel *InstancePtr, u64 Data);
u64 XMic16_wdas_compact_accel_Get_integer_delay(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_Set_combined_coeff_q16(XMic16_wdas_compact_accel *InstancePtr, u64 Data);
u64 XMic16_wdas_compact_accel_Get_combined_coeff_q16(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_Set_enhanced_pcm(XMic16_wdas_compact_accel *InstancePtr, u64 Data);
u64 XMic16_wdas_compact_accel_Get_enhanced_pcm(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_Set_frame_samples(XMic16_wdas_compact_accel *InstancePtr, u32 Data);
u32 XMic16_wdas_compact_accel_Get_frame_samples(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_Set_steering_valid(XMic16_wdas_compact_accel *InstancePtr, u32 Data);
u32 XMic16_wdas_compact_accel_Get_steering_valid(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_Set_fallback_channel(XMic16_wdas_compact_accel *InstancePtr, u32 Data);
u32 XMic16_wdas_compact_accel_Get_fallback_channel(XMic16_wdas_compact_accel *InstancePtr);

void XMic16_wdas_compact_accel_InterruptGlobalEnable(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_InterruptGlobalDisable(XMic16_wdas_compact_accel *InstancePtr);
void XMic16_wdas_compact_accel_InterruptEnable(XMic16_wdas_compact_accel *InstancePtr, u32 Mask);
void XMic16_wdas_compact_accel_InterruptDisable(XMic16_wdas_compact_accel *InstancePtr, u32 Mask);
void XMic16_wdas_compact_accel_InterruptClear(XMic16_wdas_compact_accel *InstancePtr, u32 Mask);
u32 XMic16_wdas_compact_accel_InterruptGetEnabled(XMic16_wdas_compact_accel *InstancePtr);
u32 XMic16_wdas_compact_accel_InterruptGetStatus(XMic16_wdas_compact_accel *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
