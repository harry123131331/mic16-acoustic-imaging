#ifndef MIC16_IRQ_MOCK_SDK_H
#define MIC16_IRQ_MOCK_SDK_H
/* 仅测试夹具的寄存器/GIC/缓存边界，不链接真实硬件。 */
#include <stdint.h>
#include <stddef.h>
typedef uintptr_t UINTPTR;
typedef intptr_t INTPTR;
typedef uint32_t u32;
typedef uint64_t XTime;
typedef struct {unsigned value;} XAxiDma;
typedef struct {unsigned value;} XAxiDma_Config;
typedef struct {u32 CpuBaseAddress,DistBaseAddress;} XScuGic_Config;
typedef void (*Xil_InterruptHandler)(void *);
#define XST_SUCCESS 0
#define XAXIDMA_DEVICE_TO_DMA 1
#define XAXIDMA_IRQ_ALL_MASK 0x7000u
#define XAXIDMA_IRQ_IOC_MASK 0x1000u
#define XAXIDMA_IRQ_ERROR_MASK 0x4000u
#define XAXIDMA_RX_OFFSET 0x30u
#define XAXIDMA_SR_OFFSET 4u
#define XAXIDMA_BUFFLEN_OFFSET 0x28u
#define XPAR_AXI_DMA_0_BASEADDR 0x40400000u
#define XPAR_AXI_GPIO_0_BASEADDR 0x41200000u
#define XPAR_AXI_GPIO_1_BASEADDR 0x41210000u
#define XPAR_AXI_GPIO_2_BASEADDR 0x41220000u
#define XPAR_AXI_DMA_0_INCLUDE_SG 0
#define XPAR_AXI_DMA_0_INCLUDE_S2MM_DRE 0
#define XPAR_AXI_DMA_0_DEVICE_ID 0
#define XPAR_SCUGIC_0_DEVICE_ID 0
#define XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR 61
#define COUNTS_PER_SECOND 1000000u
XAxiDma_Config *XAxiDma_LookupConfig(unsigned);
XScuGic_Config *XScuGic_LookupConfig(unsigned);
int XAxiDma_CfgInitialize(XAxiDma *,XAxiDma_Config *);
int XAxiDma_HasSg(XAxiDma *);
void XAxiDma_IntrDisable(XAxiDma *,unsigned,int);
void XAxiDma_IntrEnable(XAxiDma *,unsigned,int);
void XAxiDma_IntrAckIrq(XAxiDma *,unsigned,int);
unsigned XAxiDma_IntrGetIrq(XAxiDma *,int);
void XAxiDma_Reset(XAxiDma *);
int XAxiDma_ResetIsDone(XAxiDma *);
int XAxiDma_SimpleTransfer(XAxiDma *,UINTPTR,unsigned,int);
unsigned XAxiDma_ReadReg(UINTPTR,unsigned);
void XScuGic_DisableIntr(u32,unsigned);
void XScuGic_EnableIntr(u32,unsigned);
void XScuGic_RegisterHandler(u32,int,Xil_InterruptHandler,void *);
void XScuGic_SetPriTrigTypeByDistAddr(u32,unsigned,unsigned,unsigned);
void Xil_DCacheFlushRange(INTPTR,u32);
void Xil_DCacheInvalidateRange(INTPTR,u32);
unsigned Xil_In32(UINTPTR);
void Xil_Out32(UINTPTR,unsigned);
void XTime_GetTime(XTime *);
#endif
