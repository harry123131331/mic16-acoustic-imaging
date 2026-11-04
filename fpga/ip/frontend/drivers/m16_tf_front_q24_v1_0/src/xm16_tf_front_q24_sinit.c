// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#include "xparameters.h"
#include "xm16_tf_front_q24.h"

extern XM16_tf_front_q24_Config XM16_tf_front_q24_ConfigTable[];

XM16_tf_front_q24_Config *XM16_tf_front_q24_LookupConfig(u16 DeviceId) {
	XM16_tf_front_q24_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XM16_TF_FRONT_Q24_NUM_INSTANCES; Index++) {
		if (XM16_tf_front_q24_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XM16_tf_front_q24_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XM16_tf_front_q24_Initialize(XM16_tf_front_q24 *InstancePtr, u16 DeviceId) {
	XM16_tf_front_q24_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XM16_tf_front_q24_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XM16_tf_front_q24_CfgInitialize(InstancePtr, ConfigPtr);
}

#endif

