// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
/***************************** Include Files *********************************/
#include "xmic16_wdas_compact_accel.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XMic16_wdas_compact_accel_CfgInitialize(XMic16_wdas_compact_accel *InstancePtr, XMic16_wdas_compact_accel_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XMic16_wdas_compact_accel_Start(XMic16_wdas_compact_accel *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL) & 0x80;
    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XMic16_wdas_compact_accel_IsDone(XMic16_wdas_compact_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XMic16_wdas_compact_accel_IsIdle(XMic16_wdas_compact_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XMic16_wdas_compact_accel_IsReady(XMic16_wdas_compact_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XMic16_wdas_compact_accel_EnableAutoRestart(XMic16_wdas_compact_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XMic16_wdas_compact_accel_DisableAutoRestart(XMic16_wdas_compact_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL, 0);
}

void XMic16_wdas_compact_accel_Set_pcm(XMic16_wdas_compact_accel *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_PCM_DATA, (u32)(Data));
    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_PCM_DATA + 4, (u32)(Data >> 32));
}

u64 XMic16_wdas_compact_accel_Get_pcm(XMic16_wdas_compact_accel *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_PCM_DATA);
    Data += (u64)XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_PCM_DATA + 4) << 32;
    return Data;
}

void XMic16_wdas_compact_accel_Set_integer_delay(XMic16_wdas_compact_accel *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_INTEGER_DELAY_DATA, (u32)(Data));
    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_INTEGER_DELAY_DATA + 4, (u32)(Data >> 32));
}

u64 XMic16_wdas_compact_accel_Get_integer_delay(XMic16_wdas_compact_accel *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_INTEGER_DELAY_DATA);
    Data += (u64)XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_INTEGER_DELAY_DATA + 4) << 32;
    return Data;
}

void XMic16_wdas_compact_accel_Set_combined_coeff_q16(XMic16_wdas_compact_accel *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_COMBINED_COEFF_Q16_DATA, (u32)(Data));
    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_COMBINED_COEFF_Q16_DATA + 4, (u32)(Data >> 32));
}

u64 XMic16_wdas_compact_accel_Get_combined_coeff_q16(XMic16_wdas_compact_accel *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_COMBINED_COEFF_Q16_DATA);
    Data += (u64)XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_COMBINED_COEFF_Q16_DATA + 4) << 32;
    return Data;
}

void XMic16_wdas_compact_accel_Set_enhanced_pcm(XMic16_wdas_compact_accel *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ENHANCED_PCM_DATA, (u32)(Data));
    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ENHANCED_PCM_DATA + 4, (u32)(Data >> 32));
}

u64 XMic16_wdas_compact_accel_Get_enhanced_pcm(XMic16_wdas_compact_accel *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ENHANCED_PCM_DATA);
    Data += (u64)XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ENHANCED_PCM_DATA + 4) << 32;
    return Data;
}

void XMic16_wdas_compact_accel_Set_frame_samples(XMic16_wdas_compact_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_FRAME_SAMPLES_DATA, Data);
}

u32 XMic16_wdas_compact_accel_Get_frame_samples(XMic16_wdas_compact_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_FRAME_SAMPLES_DATA);
    return Data;
}

void XMic16_wdas_compact_accel_Set_steering_valid(XMic16_wdas_compact_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_STEERING_VALID_DATA, Data);
}

u32 XMic16_wdas_compact_accel_Get_steering_valid(XMic16_wdas_compact_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_STEERING_VALID_DATA);
    return Data;
}

void XMic16_wdas_compact_accel_Set_fallback_channel(XMic16_wdas_compact_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_FALLBACK_CHANNEL_DATA, Data);
}

u32 XMic16_wdas_compact_accel_Get_fallback_channel(XMic16_wdas_compact_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_FALLBACK_CHANNEL_DATA);
    return Data;
}

void XMic16_wdas_compact_accel_InterruptGlobalEnable(XMic16_wdas_compact_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_GIE, 1);
}

void XMic16_wdas_compact_accel_InterruptGlobalDisable(XMic16_wdas_compact_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_GIE, 0);
}

void XMic16_wdas_compact_accel_InterruptEnable(XMic16_wdas_compact_accel *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_IER);
    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_IER, Register | Mask);
}

void XMic16_wdas_compact_accel_InterruptDisable(XMic16_wdas_compact_accel *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_IER);
    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_IER, Register & (~Mask));
}

void XMic16_wdas_compact_accel_InterruptClear(XMic16_wdas_compact_accel *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_wdas_compact_accel_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ISR, Mask);
}

u32 XMic16_wdas_compact_accel_InterruptGetEnabled(XMic16_wdas_compact_accel *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_IER);
}

u32 XMic16_wdas_compact_accel_InterruptGetStatus(XMic16_wdas_compact_accel *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XMic16_wdas_compact_accel_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ISR);
}

