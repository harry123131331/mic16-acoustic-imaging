// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#include "xparameters.h"
#include "xm16_sp_shared.h"

extern XM16_sp_shared_Config XM16_sp_shared_ConfigTable[];

XM16_sp_shared_Config *XM16_sp_shared_LookupConfig(u16 DeviceId) {
	XM16_sp_shared_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XM16_SP_SHARED_NUM_INSTANCES; Index++) {
		if (XM16_sp_shared_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XM16_sp_shared_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XM16_sp_shared_Initialize(XM16_sp_shared *InstancePtr, u16 DeviceId) {
	XM16_sp_shared_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XM16_sp_shared_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XM16_sp_shared_CfgInitialize(InstancePtr, ConfigPtr);
}

#endif

