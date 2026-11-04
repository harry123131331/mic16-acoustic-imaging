// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#include "xparameters.h"
#include "xm16_shared_projection_engine.h"

extern XM16_shared_projection_engine_Config XM16_shared_projection_engine_ConfigTable[];

XM16_shared_projection_engine_Config *XM16_shared_projection_engine_LookupConfig(u16 DeviceId) {
	XM16_shared_projection_engine_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XM16_SHARED_PROJECTION_ENGINE_NUM_INSTANCES; Index++) {
		if (XM16_shared_projection_engine_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XM16_shared_projection_engine_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XM16_shared_projection_engine_Initialize(XM16_shared_projection_engine *InstancePtr, u16 DeviceId) {
	XM16_shared_projection_engine_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XM16_shared_projection_engine_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XM16_shared_projection_engine_CfgInitialize(InstancePtr, ConfigPtr);
}

#endif

