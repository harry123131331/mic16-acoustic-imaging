// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#include "xparameters.h"
#include "xmic16_tf_zone_streaming_hls_q2_24.h"

extern XMic16_tf_zone_streaming_hls_q2_24_Config XMic16_tf_zone_streaming_hls_q2_24_ConfigTable[];

XMic16_tf_zone_streaming_hls_q2_24_Config *XMic16_tf_zone_streaming_hls_q2_24_LookupConfig(u16 DeviceId) {
	XMic16_tf_zone_streaming_hls_q2_24_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_NUM_INSTANCES; Index++) {
		if (XMic16_tf_zone_streaming_hls_q2_24_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XMic16_tf_zone_streaming_hls_q2_24_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XMic16_tf_zone_streaming_hls_q2_24_Initialize(XMic16_tf_zone_streaming_hls_q2_24 *InstancePtr, u16 DeviceId) {
	XMic16_tf_zone_streaming_hls_q2_24_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XMic16_tf_zone_streaming_hls_q2_24_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XMic16_tf_zone_streaming_hls_q2_24_CfgInitialize(InstancePtr, ConfigPtr);
}

#endif

