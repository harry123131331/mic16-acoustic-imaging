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
// 0x10 : Data signal of ap_return
//        bit 31~0 - ap_return[31:0] (Read)
// 0x18 : Data signal of ddr_arena
//        bit 31~0 - ddr_arena[31:0] (Read/Write)
// 0x1c : Data signal of ddr_arena
//        bit 31~0 - ddr_arena[63:32] (Read/Write)
// 0x20 : reserved
// 0x24 : Data signal of pcm_word_offset
//        bit 31~0 - pcm_word_offset[31:0] (Read/Write)
// 0x28 : reserved
// 0x2c : Data signal of workspace_word_offset
//        bit 31~0 - workspace_word_offset[31:0] (Read/Write)
// 0x30 : reserved
// 0x34 : Data signal of frame_samples
//        bit 31~0 - frame_samples[31:0] (Read/Write)
// 0x38 : reserved
// 0x3c : Data signal of mode
//        bit 31~0 - mode[31:0] (Read/Write)
// 0x40 : reserved
// 0x44 : Data signal of active_channels
//        bit 31~0 - active_channels[31:0] (Read/Write)
// 0x48 : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XM16_SP_SHARED_CONTROL_ADDR_AP_CTRL                    0x00
#define XM16_SP_SHARED_CONTROL_ADDR_GIE                        0x04
#define XM16_SP_SHARED_CONTROL_ADDR_IER                        0x08
#define XM16_SP_SHARED_CONTROL_ADDR_ISR                        0x0c
#define XM16_SP_SHARED_CONTROL_ADDR_AP_RETURN                  0x10
#define XM16_SP_SHARED_CONTROL_BITS_AP_RETURN                  32
#define XM16_SP_SHARED_CONTROL_ADDR_DDR_ARENA_DATA             0x18
#define XM16_SP_SHARED_CONTROL_BITS_DDR_ARENA_DATA             64
#define XM16_SP_SHARED_CONTROL_ADDR_PCM_WORD_OFFSET_DATA       0x24
#define XM16_SP_SHARED_CONTROL_BITS_PCM_WORD_OFFSET_DATA       32
#define XM16_SP_SHARED_CONTROL_ADDR_WORKSPACE_WORD_OFFSET_DATA 0x2c
#define XM16_SP_SHARED_CONTROL_BITS_WORKSPACE_WORD_OFFSET_DATA 32
#define XM16_SP_SHARED_CONTROL_ADDR_FRAME_SAMPLES_DATA         0x34
#define XM16_SP_SHARED_CONTROL_BITS_FRAME_SAMPLES_DATA         32
#define XM16_SP_SHARED_CONTROL_ADDR_MODE_DATA                  0x3c
#define XM16_SP_SHARED_CONTROL_BITS_MODE_DATA                  32
#define XM16_SP_SHARED_CONTROL_ADDR_ACTIVE_CHANNELS_DATA       0x44
#define XM16_SP_SHARED_CONTROL_BITS_ACTIVE_CHANNELS_DATA       32

