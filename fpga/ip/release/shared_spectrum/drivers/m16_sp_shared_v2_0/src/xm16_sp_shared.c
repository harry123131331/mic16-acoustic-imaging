// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
/***************************** Include Files *********************************/
#include "xm16_sp_shared.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XM16_sp_shared_CfgInitialize(XM16_sp_shared *InstancePtr, XM16_sp_shared_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XM16_sp_shared_Start(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL) & 0x80;
    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XM16_sp_shared_IsDone(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XM16_sp_shared_IsIdle(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XM16_sp_shared_IsReady(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XM16_sp_shared_EnableAutoRestart(XM16_sp_shared *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XM16_sp_shared_DisableAutoRestart(XM16_sp_shared *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL, 0);
}

u32 XM16_sp_shared_Get_return(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_AP_RETURN);
    return Data;
}
void XM16_sp_shared_Set_ddr_arena(XM16_sp_shared *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_DDR_ARENA_DATA, (u32)(Data));
    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_DDR_ARENA_DATA + 4, (u32)(Data >> 32));
}

u64 XM16_sp_shared_Get_ddr_arena(XM16_sp_shared *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_DDR_ARENA_DATA);
    Data += (u64)XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_DDR_ARENA_DATA + 4) << 32;
    return Data;
}

void XM16_sp_shared_Set_pcm_word_offset(XM16_sp_shared *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_PCM_WORD_OFFSET_DATA, Data);
}

u32 XM16_sp_shared_Get_pcm_word_offset(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_PCM_WORD_OFFSET_DATA);
    return Data;
}

void XM16_sp_shared_Set_workspace_word_offset(XM16_sp_shared *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_WORKSPACE_WORD_OFFSET_DATA, Data);
}

u32 XM16_sp_shared_Get_workspace_word_offset(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_WORKSPACE_WORD_OFFSET_DATA);
    return Data;
}

void XM16_sp_shared_Set_frame_samples(XM16_sp_shared *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_FRAME_SAMPLES_DATA, Data);
}

u32 XM16_sp_shared_Get_frame_samples(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_FRAME_SAMPLES_DATA);
    return Data;
}

void XM16_sp_shared_Set_mode(XM16_sp_shared *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_MODE_DATA, Data);
}

u32 XM16_sp_shared_Get_mode(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_MODE_DATA);
    return Data;
}

void XM16_sp_shared_Set_active_channels(XM16_sp_shared *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_ACTIVE_CHANNELS_DATA, Data);
}

u32 XM16_sp_shared_Get_active_channels(XM16_sp_shared *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_ACTIVE_CHANNELS_DATA);
    return Data;
}

void XM16_sp_shared_InterruptGlobalEnable(XM16_sp_shared *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_GIE, 1);
}

void XM16_sp_shared_InterruptGlobalDisable(XM16_sp_shared *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_GIE, 0);
}

void XM16_sp_shared_InterruptEnable(XM16_sp_shared *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_IER);
    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_IER, Register | Mask);
}

void XM16_sp_shared_InterruptDisable(XM16_sp_shared *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_IER);
    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_IER, Register & (~Mask));
}

void XM16_sp_shared_InterruptClear(XM16_sp_shared *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XM16_sp_shared_WriteReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_ISR, Mask);
}

u32 XM16_sp_shared_InterruptGetEnabled(XM16_sp_shared *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_IER);
}

u32 XM16_sp_shared_InterruptGetStatus(XM16_sp_shared *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XM16_sp_shared_ReadReg(InstancePtr->Control_BaseAddress, XM16_SP_SHARED_CONTROL_ADDR_ISR);
}

