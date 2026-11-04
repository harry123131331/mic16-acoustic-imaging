// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#include "xparameters.h"
#include "xmic16_wdas_compact_accel.h"

extern XMic16_wdas_compact_accel_Config XMic16_wdas_compact_accel_ConfigTable[];

XMic16_wdas_compact_accel_Config *XMic16_wdas_compact_accel_LookupConfig(u16 DeviceId) {
	XMic16_wdas_compact_accel_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XMIC16_WDAS_COMPACT_ACCEL_NUM_INSTANCES; Index++) {
		if (XMic16_wdas_compact_accel_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XMic16_wdas_compact_accel_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XMic16_wdas_compact_accel_Initialize(XMic16_wdas_compact_accel *InstancePtr, u16 DeviceId) {
	XMic16_wdas_compact_accel_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XMic16_wdas_compact_accel_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XMic16_wdas_compact_accel_CfgInitialize(InstancePtr, ConfigPtr);
}

#endif

