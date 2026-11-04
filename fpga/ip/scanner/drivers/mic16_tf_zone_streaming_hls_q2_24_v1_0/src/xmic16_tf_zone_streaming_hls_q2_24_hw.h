// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// ==============================================================
// control
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read)
//        bit 7  - auto_restart (Read/Write)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0  - enable ap_done interrupt (Read/Write)
//        bit 1  - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0  - ap_done (COR/TOW)
//        bit 1  - ap_ready (COR/TOW)
//        others - reserved
// 0x10 : Data signal of ddr_arena
//        bit 31~0 - ddr_arena[31:0] (Read/Write)
// 0x14 : Data signal of ddr_arena
//        bit 31~0 - ddr_arena[63:32] (Read/Write)
// 0x18 : reserved
// 0x1c : Data signal of observation_offset
//        bit 31~0 - observation_offset[31:0] (Read/Write)
// 0x20 : reserved
// 0x24 : Data signal of initial_phasor_offset
//        bit 31~0 - initial_phasor_offset[31:0] (Read/Write)
// 0x28 : reserved
// 0x2c : Data signal of bin_rotator_offset
//        bit 31~0 - bin_rotator_offset[31:0] (Read/Write)
// 0x30 : reserved
// 0x34 : Data signal of output_offset
//        bit 31~0 - output_offset[31:0] (Read/Write)
// 0x38 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_AP_CTRL                    0x00
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_GIE                        0x04
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_IER                        0x08
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_ISR                        0x0c
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_DDR_ARENA_DATA             0x10
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_BITS_DDR_ARENA_DATA             64
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_OBSERVATION_OFFSET_DATA    0x1c
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_BITS_OBSERVATION_OFFSET_DATA    32
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_INITIAL_PHASOR_OFFSET_DATA 0x24
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_BITS_INITIAL_PHASOR_OFFSET_DATA 32
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_BIN_ROTATOR_OFFSET_DATA    0x2c
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_BITS_BIN_ROTATOR_OFFSET_DATA    32
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_ADDR_OUTPUT_OFFSET_DATA         0x34
#define XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_CONTROL_BITS_OUTPUT_OFFSET_DATA         32

