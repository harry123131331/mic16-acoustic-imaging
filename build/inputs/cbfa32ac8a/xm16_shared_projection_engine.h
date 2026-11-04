// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef XM16_SHARED_PROJECTION_ENGINE_H
#define XM16_SHARED_PROJECTION_ENGINE_H

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
#include "xm16_shared_projection_engine_hw.h"

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
} XM16_shared_projection_engine_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XM16_shared_projection_engine;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XM16_shared_projection_engine_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XM16_shared_projection_engine_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XM16_shared_projection_engine_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XM16_shared_projection_engine_ReadReg(BaseAddress, RegOffset) \
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
int XM16_shared_projection_engine_Initialize(XM16_shared_projection_engine *InstancePtr, u16 DeviceId);
XM16_shared_projection_engine_Config* XM16_shared_projection_engine_LookupConfig(u16 DeviceId);
int XM16_shared_projection_engine_CfgInitialize(XM16_shared_projection_engine *InstancePtr, XM16_shared_projection_engine_Config *ConfigPtr);
#else
int XM16_shared_projection_engine_Initialize(XM16_shared_projection_engine *InstancePtr, const char* InstanceName);
int XM16_shared_projection_engine_Release(XM16_shared_projection_engine *InstancePtr);
#endif

void XM16_shared_projection_engine_Start(XM16_shared_projection_engine *InstancePtr);
u32 XM16_shared_projection_engine_IsDone(XM16_shared_projection_engine *InstancePtr);
u32 XM16_shared_projection_engine_IsIdle(XM16_shared_projection_engine *InstancePtr);
u32 XM16_shared_projection_engine_IsReady(XM16_shared_projection_engine *InstancePtr);
void XM16_shared_projection_engine_EnableAutoRestart(XM16_shared_projection_engine *InstancePtr);
void XM16_shared_projection_engine_DisableAutoRestart(XM16_shared_projection_engine *InstancePtr);
u32 XM16_shared_projection_engine_Get_return(XM16_shared_projection_engine *InstancePtr);

void XM16_shared_projection_engine_Set_arena(XM16_shared_projection_engine *InstancePtr, u64 Data);
u64 XM16_shared_projection_engine_Get_arena(XM16_shared_projection_engine *InstancePtr);
void XM16_shared_projection_engine_Set_arena_words(XM16_shared_projection_engine *InstancePtr, u32 Data);
u32 XM16_shared_projection_engine_Get_arena_words(XM16_shared_projection_engine *InstancePtr);
void XM16_shared_projection_engine_Set_descriptors(XM16_shared_projection_engine *InstancePtr, u32 Data);
u32 XM16_shared_projection_engine_Get_descriptors(XM16_shared_projection_engine *InstancePtr);
void XM16_shared_projection_engine_Set_jobs(XM16_shared_projection_engine *InstancePtr, u32 Data);
u32 XM16_shared_projection_engine_Get_jobs(XM16_shared_projection_engine *InstancePtr);

void XM16_shared_projection_engine_InterruptGlobalEnable(XM16_shared_projection_engine *InstancePtr);
void XM16_shared_projection_engine_InterruptGlobalDisable(XM16_shared_projection_engine *InstancePtr);
void XM16_shared_projection_engine_InterruptEnable(XM16_shared_projection_engine *InstancePtr, u32 Mask);
void XM16_shared_projection_engine_InterruptDisable(XM16_shared_projection_engine *InstancePtr, u32 Mask);
void XM16_shared_projection_engine_InterruptClear(XM16_shared_projection_engine *InstancePtr, u32 Mask);
u32 XM16_shared_projection_engine_InterruptGetEnabled(XM16_shared_projection_engine *InstancePtr);
u32 XM16_shared_projection_engine_InterruptGetStatus(XM16_shared_projection_engine *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
