// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef XM16_SP_SHARED_H
#define XM16_SP_SHARED_H

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
#include "xm16_sp_shared_hw.h"

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
} XM16_sp_shared_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XM16_sp_shared;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XM16_sp_shared_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XM16_sp_shared_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XM16_sp_shared_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XM16_sp_shared_ReadReg(BaseAddress, RegOffset) \
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
int XM16_sp_shared_Initialize(XM16_sp_shared *InstancePtr, u16 DeviceId);
XM16_sp_shared_Config* XM16_sp_shared_LookupConfig(u16 DeviceId);
int XM16_sp_shared_CfgInitialize(XM16_sp_shared *InstancePtr, XM16_sp_shared_Config *ConfigPtr);
#else
int XM16_sp_shared_Initialize(XM16_sp_shared *InstancePtr, const char* InstanceName);
int XM16_sp_shared_Release(XM16_sp_shared *InstancePtr);
#endif

void XM16_sp_shared_Start(XM16_sp_shared *InstancePtr);
u32 XM16_sp_shared_IsDone(XM16_sp_shared *InstancePtr);
u32 XM16_sp_shared_IsIdle(XM16_sp_shared *InstancePtr);
u32 XM16_sp_shared_IsReady(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_EnableAutoRestart(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_DisableAutoRestart(XM16_sp_shared *InstancePtr);
u32 XM16_sp_shared_Get_return(XM16_sp_shared *InstancePtr);

void XM16_sp_shared_Set_ddr_arena(XM16_sp_shared *InstancePtr, u64 Data);
u64 XM16_sp_shared_Get_ddr_arena(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_Set_pcm_word_offset(XM16_sp_shared *InstancePtr, u32 Data);
u32 XM16_sp_shared_Get_pcm_word_offset(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_Set_workspace_word_offset(XM16_sp_shared *InstancePtr, u32 Data);
u32 XM16_sp_shared_Get_workspace_word_offset(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_Set_frame_samples(XM16_sp_shared *InstancePtr, u32 Data);
u32 XM16_sp_shared_Get_frame_samples(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_Set_mode(XM16_sp_shared *InstancePtr, u32 Data);
u32 XM16_sp_shared_Get_mode(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_Set_active_channels(XM16_sp_shared *InstancePtr, u32 Data);
u32 XM16_sp_shared_Get_active_channels(XM16_sp_shared *InstancePtr);

void XM16_sp_shared_InterruptGlobalEnable(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_InterruptGlobalDisable(XM16_sp_shared *InstancePtr);
void XM16_sp_shared_InterruptEnable(XM16_sp_shared *InstancePtr, u32 Mask);
void XM16_sp_shared_InterruptDisable(XM16_sp_shared *InstancePtr, u32 Mask);
void XM16_sp_shared_InterruptClear(XM16_sp_shared *InstancePtr, u32 Mask);
u32 XM16_sp_shared_InterruptGetEnabled(XM16_sp_shared *InstancePtr);
u32 XM16_sp_shared_InterruptGetStatus(XM16_sp_shared *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
