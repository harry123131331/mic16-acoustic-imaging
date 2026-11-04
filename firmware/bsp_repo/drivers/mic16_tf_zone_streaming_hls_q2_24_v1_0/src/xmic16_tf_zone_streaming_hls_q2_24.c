// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
/***************************** Include Files *********************************/
#include "xmic16_tf_zone_streaming_hls_q2_24.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XMic16_tf_zone_streaming_hls_q2_24_CfgInitialize(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, XMic16_tf_zone_streaming_hls_q2_24_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XMic16_tf_zone_streaming_hls_q2_24_Start(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL) & 0x80;
    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XMic16_tf_zone_streaming_hls_q2_24_IsDone(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XMic16_tf_zone_streaming_hls_q2_24_IsIdle(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XMic16_tf_zone_streaming_hls_q2_24_IsReady(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XMic16_tf_zone_streaming_hls_q2_24_EnableAutoRestart(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XMic16_tf_zone_streaming_hls_q2_24_DisableAutoRestart(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL, 0);
}

void XMic16_tf_zone_streaming_hls_q2_24_Set_ddr_arena(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_DDR_ARENA_DATA, (u32)(Data));
    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_DDR_ARENA_DATA + 4, (u32)(Data >> 32));
}

u64 XMic16_tf_zone_streaming_hls_q2_24_Get_ddr_arena(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_DDR_ARENA_DATA);
    Data += (u64)XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_DDR_ARENA_DATA + 4) << 32;
    return Data;
}

void XMic16_tf_zone_streaming_hls_q2_24_Set_observation_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_OBSERVATION_OFFSET_DATA, Data);
}

u32 XMic16_tf_zone_streaming_hls_q2_24_Get_observation_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_OBSERVATION_OFFSET_DATA);
    return Data;
}

void XMic16_tf_zone_streaming_hls_q2_24_Set_initial_phasor_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_INITIAL_PHASOR_OFFSET_DATA, Data);
}

u32 XMic16_tf_zone_streaming_hls_q2_24_Get_initial_phasor_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_INITIAL_PHASOR_OFFSET_DATA);
    return Data;
}

void XMic16_tf_zone_streaming_hls_q2_24_Set_bin_rotator_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_BIN_ROTATOR_OFFSET_DATA, Data);
}

u32 XMic16_tf_zone_streaming_hls_q2_24_Get_bin_rotator_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_BIN_ROTATOR_OFFSET_DATA);
    return Data;
}

void XMic16_tf_zone_streaming_hls_q2_24_Set_output_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_OUTPUT_OFFSET_DATA, Data);
}

u32 XMic16_tf_zone_streaming_hls_q2_24_Get_output_offset(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_OUTPUT_OFFSET_DATA);
    return Data;
}

void XMic16_tf_zone_streaming_hls_q2_24_InterruptGlobalEnable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_GIE, 1);
}

void XMic16_tf_zone_streaming_hls_q2_24_InterruptGlobalDisable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_GIE, 0);
}

void XMic16_tf_zone_streaming_hls_q2_24_InterruptEnable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_IER);
    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_IER, Register | Mask);
}

void XMic16_tf_zone_streaming_hls_q2_24_InterruptDisable(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_IER);
    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_IER, Register & (~Mask));
}

void XMic16_tf_zone_streaming_hls_q2_24_InterruptClear(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XMic16_tf_zone_streaming_hls_q2_24_WriteReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_ISR, Mask);
}

u32 XMic16_tf_zone_streaming_hls_q2_24_InterruptGetEnabled(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_IER);
}

u32 XMic16_tf_zone_streaming_hls_q2_24_InterruptGetStatus(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XMic16_tf_zone_streaming_hls_q2_24_ReadReg(InstancePtr->Control_BaseAddress, XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_ISR);
}

