// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
/***************************** Include Files *********************************/
#include "xm16_shared_projection_engine.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XM16_shared_projection_engine_CfgInitialize(XM16_shared_projection_engine *InstancePtr, XM16_shared_projection_engine_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XM16_shared_projection_engine_Start(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_CTRL) & 0x80;
    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XM16_shared_projection_engine_IsDone(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XM16_shared_projection_engine_IsIdle(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XM16_shared_projection_engine_IsReady(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XM16_shared_projection_engine_EnableAutoRestart(XM16_shared_projection_engine *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XM16_shared_projection_engine_DisableAutoRestart(XM16_shared_projection_engine *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_CTRL, 0);
}

u32 XM16_shared_projection_engine_Get_return(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_AP_RETURN);
    return Data;
}
void XM16_shared_projection_engine_Set_arena(XM16_shared_projection_engine *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ARENA_DATA, (u32)(Data));
    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ARENA_DATA + 4, (u32)(Data >> 32));
}

u64 XM16_shared_projection_engine_Get_arena(XM16_shared_projection_engine *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ARENA_DATA);
    Data += (u64)XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ARENA_DATA + 4) << 32;
    return Data;
}

void XM16_shared_projection_engine_Set_arena_words(XM16_shared_projection_engine *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ARENA_WORDS_DATA, Data);
}

u32 XM16_shared_projection_engine_Get_arena_words(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ARENA_WORDS_DATA);
    return Data;
}

void XM16_shared_projection_engine_Set_descriptors(XM16_shared_projection_engine *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_DESCRIPTORS_DATA, Data);
}

u32 XM16_shared_projection_engine_Get_descriptors(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_DESCRIPTORS_DATA);
    return Data;
}

void XM16_shared_projection_engine_Set_jobs(XM16_shared_projection_engine *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_JOBS_DATA, Data);
}

u32 XM16_shared_projection_engine_Get_jobs(XM16_shared_projection_engine *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_JOBS_DATA);
    return Data;
}

void XM16_shared_projection_engine_InterruptGlobalEnable(XM16_shared_projection_engine *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_GIE, 1);
}

void XM16_shared_projection_engine_InterruptGlobalDisable(XM16_shared_projection_engine *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_GIE, 0);
}

void XM16_shared_projection_engine_InterruptEnable(XM16_shared_projection_engine *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_IER);
    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_IER, Register | Mask);
}

void XM16_shared_projection_engine_InterruptDisable(XM16_shared_projection_engine *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_IER);
    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_IER, Register & (~Mask));
}

void XM16_shared_projection_engine_InterruptClear(XM16_shared_projection_engine *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_shared_projection_engine_WriteReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ISR, Mask);
}

u32 XM16_shared_projection_engine_InterruptGetEnabled(XM16_shared_projection_engine *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_IER);
}

u32 XM16_shared_projection_engine_InterruptGetStatus(XM16_shared_projection_engine *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XM16_shared_projection_engine_ReadReg(InstancePtr->Control_BaseAddress, XM16_SHARED_PROJECTION_ENGINE_CONTROL_ADDR_ISR);
}

