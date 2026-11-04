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
// 0x10 : Data signal of pcm
//        bit 31~0 - pcm[31:0] (Read/Write)
// 0x14 : Data signal of pcm
//        bit 31~0 - pcm[63:32] (Read/Write)
// 0x18 : reserved
// 0x1c : Data signal of integer_delay
//        bit 31~0 - integer_delay[31:0] (Read/Write)
// 0x20 : Data signal of integer_delay
//        bit 31~0 - integer_delay[63:32] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of combined_coeff_q16
//        bit 31~0 - combined_coeff_q16[31:0] (Read/Write)
// 0x2c : Data signal of combined_coeff_q16
//        bit 31~0 - combined_coeff_q16[63:32] (Read/Write)
// 0x30 : reserved
// 0x34 : Data signal of enhanced_pcm
//        bit 31~0 - enhanced_pcm[31:0] (Read/Write)
// 0x38 : Data signal of enhanced_pcm
//        bit 31~0 - enhanced_pcm[63:32] (Read/Write)
// 0x3c : reserved
// 0x40 : Data signal of frame_samples
//        bit 31~0 - frame_samples[31:0] (Read/Write)
// 0x44 : reserved
// 0x48 : Data signal of steering_valid
//        bit 31~0 - steering_valid[31:0] (Read/Write)
// 0x4c : reserved
// 0x50 : Data signal of fallback_channel
//        bit 31~0 - fallback_channel[31:0] (Read/Write)
// 0x54 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_AP_CTRL                 0x00
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_GIE                     0x04
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_IER                     0x08
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ISR                     0x0c
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_PCM_DATA                0x10
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_BITS_PCM_DATA                64
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_INTEGER_DELAY_DATA      0x1c
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_BITS_INTEGER_DELAY_DATA      64
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_COMBINED_COEFF_Q16_DATA 0x28
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_BITS_COMBINED_COEFF_Q16_DATA 64
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_ENHANCED_PCM_DATA       0x34
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_BITS_ENHANCED_PCM_DATA       64
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_FRAME_SAMPLES_DATA      0x40
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_BITS_FRAME_SAMPLES_DATA      32
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_STEERING_VALID_DATA     0x48
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_BITS_STEERING_VALID_DATA     32
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_ADDR_FALLBACK_CHANNEL_DATA   0x50
#define XMIC16_WDAS_COMPACT_ACCEL_CONTROL_BITS_FALLBACK_CHANNEL_DATA   32

