#include <string.h>

#include "sleep.h"
#include "xaxivdma.h"
#include "xcolor_convert.h"
#include "xgpio.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xpixel_unpack.h"
#include "xstatus.h"
#include "xvidc.h"
#include "xvtc.h"
#include "xiic.h"

#define FRAME_COUNT                  4U
#define BYTES_PER_PIXEL              4U
#define ACTIVE_WIDTH                 1280U
#define ACTIVE_HEIGHT                720U
#define STRIDE_BYTES                 (ACTIVE_WIDTH * BYTES_PER_PIXEL)
#define FRAME_SIZE_BYTES             (STRIDE_BYTES * ACTIVE_HEIGHT)
#define FRAME_SPACING_BYTES          (FRAME_SIZE_BYTES + 0x1000U)
#define FRAME_BASE_ADDR              (DDR_BASE_ADDR + 0x01000000U)
#define TARGET_VIDEO_MODE            XVIDC_VM_1280x720_60_P


#define PCIE_CAPTURE_FSYNC_SOURCE    XAXIVDMA_S2MM_TUSER_FSYNC
#define DISPLAY_READ_FRAME_DELAY     1U
#define DISPLAY_POINT_NUM            0U

#if defined(XPAR_MIG_0_BASEADDRESS)
#define DDR_BASE_ADDR                XPAR_MIG_0_BASEADDRESS
#elif defined(XPAR_DDR_HIER_MIG_7SERIES_0_BASEADDR)
#define DDR_BASE_ADDR                XPAR_DDR_HIER_MIG_7SERIES_0_BASEADDR
#else
#error "No DDR base address macro found in xparameters.h"
#endif

#if defined(XPAR_AXI_VDMA_0_BASEADDR)
#define VDMA_BASE_ADDR               XPAR_AXI_VDMA_0_BASEADDR
#elif defined(XPAR_XAXIVDMA_0_BASEADDR)
#define VDMA_BASE_ADDR               XPAR_XAXIVDMA_0_BASEADDR
#else
#error "No VDMA base address macro found in xparameters.h"
#endif

#if defined(XPAR_HDMI_OUT_V_TC_0_BASEADDR)
#define VTC_BASE_ADDR                XPAR_HDMI_OUT_V_TC_0_BASEADDR
#elif defined(XPAR_XVTC_0_BASEADDR)
#define VTC_BASE_ADDR                XPAR_XVTC_0_BASEADDR
#else
#error "No VTC base address macro found in xparameters.h"
#endif

#if defined(XPAR_AXI_IIC_0_BASEADDR)
#define IIC_BASE_ADDR                XPAR_AXI_IIC_0_BASEADDR
#elif defined(XPAR_XIIC_0_BASEADDR)
#define IIC_BASE_ADDR                XPAR_XIIC_0_BASEADDR
#else
#error "No IIC base address macro found in xparameters.h"
#endif

#if defined(XPAR_HDMI_OUT_PIXEL_UNPACK_BASEADDR)
#define PIXEL_UNPACK_BASE_ADDR       XPAR_HDMI_OUT_PIXEL_UNPACK_BASEADDR
#elif defined(XPAR_XPIXEL_UNPACK_0_BASEADDR)
#define PIXEL_UNPACK_BASE_ADDR       XPAR_XPIXEL_UNPACK_0_BASEADDR
#else
#error "No pixel unpack base address macro found in xparameters.h"
#endif

#if defined(XPAR_HDMI_OUT_COLOR_CONVERT_BASEADDR)
#define COLOR_CONVERT_BASE_ADDR      XPAR_HDMI_OUT_COLOR_CONVERT_BASEADDR
#elif defined(XPAR_XCOLOR_CONVERT_0_BASEADDR)
#define COLOR_CONVERT_BASE_ADDR      XPAR_XCOLOR_CONVERT_0_BASEADDR
#else
#error "No color convert base address macro found in xparameters.h"
#endif

static XAxiVdma Vdma;
static XVtc Vtc;
static XPixel_unpack PixelUnpack;
static XColor_convert ColorConvert;
static XGpio VideoLockMonitor;
static XIic Iic;

static UINTPTR FrameAddr[FRAME_COUNT];

#define VIDEO_LOCK_MONITOR_CH1            1U
#define VIDEO_LOCK_MONITOR_CH2            2U
#define VIDEO_LOCK_MONITOR_CH1_MASK       0x0FU
#define VIDEO_LOCK_MONITOR_CH2_VTG_LAG(x) (((x) >> 16) & 0xFFFFU)

typedef struct {
    u8 dev_addr8;
    u16 reg_addr;
    u8 reg_data;
} i2c_lut_entry_t;

static const i2c_lut_entry_t HdmiLut[] = {
    {0x72, 0x0008, 0x35},
    {0x7A, 0x002F, 0x00},
    {0xFF, 0xFFFF, 0xFF}
};

static int InitVideoLockMonitor(XGpio *InstancePtr)
{
    int Status;

    Status = XGpio_Initialize(InstancePtr, (UINTPTR)XPAR_XGPIO_0_BASEADDR);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Video lock monitor GPIO init failed (%d)\r\n", Status);
        return XST_FAILURE;
    }

    XGpio_SetDataDirection(InstancePtr, VIDEO_LOCK_MONITOR_CH1,
                           VIDEO_LOCK_MONITOR_CH1_MASK);
    XGpio_SetDataDirection(InstancePtr, VIDEO_LOCK_MONITOR_CH2, 0xFFFFFFFFU);

    return XST_SUCCESS;
}

static void PrintVideoLockStateBits(u32 StatusWord)
{
    u32 StateBits;
    int Printed;

    StateBits = StatusWord & 0x1FFFU;
    Printed = 0;

    xil_printf("active_states=");

    if ((StateBits & (1U << 0)) != 0U) { xil_printf("%sIdle", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 1)) != 0U) { xil_printf("%sCourseAlignWaitVtgSof", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 2)) != 0U) { xil_printf("%sCourseAlignWaitFifoSof", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 3)) != 0U) { xil_printf("%sFineAlignVtgEolLeading", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 4)) != 0U) { xil_printf("%sFineAlignVtgEolLagging", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 5)) != 0U) { xil_printf("%sFineAlignVtgSofLeading", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 6)) != 0U) { xil_printf("%sFineAlignVtgSofLagging", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 7)) != 0U) { xil_printf("%sFineAlignActive", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 8)) != 0U) { xil_printf("%sFineAlignLocked", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 9)) != 0U) { xil_printf("%sLostAlignVtgEolLeading", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 10)) != 0U) { xil_printf("%sLostAlignVtgEolLagging", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 11)) != 0U) { xil_printf("%sLostAlignVtgSofLeading", Printed ? "," : ""); Printed = 1; }
    if ((StateBits & (1U << 12)) != 0U) { xil_printf("%sLostAlignVtgSofLagging", Printed ? "," : ""); Printed = 1; }

    if (Printed == 0) {
        xil_printf("None");
    }
}

static void PrintVideoLockMonitor(const char *StepLabel)
{
    u32 Ch1;
    u32 Ch2;

    Ch1 = XGpio_DiscreteRead(&VideoLockMonitor, VIDEO_LOCK_MONITOR_CH1) &
          VIDEO_LOCK_MONITOR_CH1_MASK;
    Ch2 = XGpio_DiscreteRead(&VideoLockMonitor, VIDEO_LOCK_MONITOR_CH2);

    xil_printf("[%s] HDMI lock ch1=0x%01lx {locked=%lu overflow=%lu underflow=%lu sof_state=%lu}\r\n",
               StepLabel,
               Ch1,
               (Ch1 >> 0) & 1U,
               (Ch1 >> 1) & 1U,
               (Ch1 >> 2) & 1U,
               (Ch1 >> 3) & 1U);

    xil_printf("[%s] HDMI lock ch2=0x%08lx {",
               StepLabel,
               Ch2);
    PrintVideoLockStateBits(Ch2);
    xil_printf(" vtg_lag=%lu}\r\n", VIDEO_LOCK_MONITOR_CH2_VTG_LAG(Ch2));
}

static void BuildFrameAddresses(void)
{
    u32 i;

    xil_printf("DDR frame ring base: 0x%08lx\r\n", (u32)FRAME_BASE_ADDR);
    for (i = 0U; i < FRAME_COUNT; ++i) {
        FrameAddr[i] = (UINTPTR)(FRAME_BASE_ADDR + (i * FRAME_SPACING_BYTES));
        xil_printf("  Frame[%lu] @ 0x%08lx\r\n", (u32)i, (u32)FrameAddr[i]);
    }
}

static void ClearFrameRing(void)
{
    u32 i;

    for (i = 0U; i < FRAME_COUNT; ++i) {
        (void)memset((void *)(UINTPTR)FrameAddr[i], 0, FRAME_SIZE_BYTES);
        Xil_DCacheFlushRange(FrameAddr[i], FRAME_SIZE_BYTES);
    }
}

static int LookupAndInitVdma(XAxiVdma *InstancePtr)
{
    XAxiVdma_Config *Cfg;

    Cfg = XAxiVdma_LookupConfig((UINTPTR)VDMA_BASE_ADDR);
    if (Cfg == NULL) {
        xil_printf("ERROR: VDMA config lookup failed\r\n");
        return XST_FAILURE;
    }

    if (XAxiVdma_CfgInitialize(InstancePtr, Cfg, Cfg->BaseAddress) != XST_SUCCESS) {
        xil_printf("ERROR: VDMA init failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("VDMA initialized: base=0x%08lx mm2s=%d s2mm=%d use_fsync=%d mm2s_genlock=%d s2mm_genlock=%d internal_genlock=%d\r\n",
               (u32)Cfg->BaseAddress,
               Cfg->HasMm2S,
               Cfg->HasS2Mm,
               Cfg->UseFsync,
               Cfg->Mm2SGenLock,
               Cfg->S2MmGenLock,
               Cfg->InternalGenLock);

    return XST_SUCCESS;
}

static int LookupAndInitVtc(XVtc *InstancePtr)
{
    XVtc_Config *Cfg;

    Cfg = XVtc_LookupConfig((UINTPTR)VTC_BASE_ADDR);
    if (Cfg == NULL) {
        xil_printf("ERROR: VTC config lookup failed\r\n");
        return XST_FAILURE;
    }

    if (XVtc_CfgInitialize(InstancePtr, Cfg, Cfg->BaseAddress) != XST_SUCCESS) {
        xil_printf("ERROR: VTC init failed\r\n");
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

static int InitPixelUnpack(XPixel_unpack *InstancePtr)
{
    int Status;

    Status = XPixel_unpack_Initialize(InstancePtr, (UINTPTR)PIXEL_UNPACK_BASE_ADDR);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Pixel unpack init failed (%d)\r\n", Status);
        return XST_FAILURE;
    }

    XPixel_unpack_Set_mode(InstancePtr, 1U);
    return XST_SUCCESS;
}

static int InitColorConvert(XColor_convert *InstancePtr)
{
    int Status;

    Status = XColor_convert_Initialize(InstancePtr, (UINTPTR)COLOR_CONVERT_BASE_ADDR);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Color convert init failed (%d)\r\n", Status);
        return XST_FAILURE;
    }

    XColor_convert_Set_c1_c1(InstancePtr, 256U);
    XColor_convert_Set_c1_c2(InstancePtr, 0U);
    XColor_convert_Set_c1_c3(InstancePtr, 0U);
    XColor_convert_Set_c2_c1(InstancePtr, 0U);
    XColor_convert_Set_c2_c2(InstancePtr, 256U);
    XColor_convert_Set_c2_c3(InstancePtr, 0U);
    XColor_convert_Set_c3_c1(InstancePtr, 0U);
    XColor_convert_Set_c3_c2(InstancePtr, 0U);
    XColor_convert_Set_c3_c3(InstancePtr, 256U);
    XColor_convert_Set_bias_c1(InstancePtr, 0U);
    XColor_convert_Set_bias_c2(InstancePtr, 0U);
    XColor_convert_Set_bias_c3(InstancePtr, 0U);

    return XST_SUCCESS;
}

static int ConfigureVtcGenerator(void)
{
    XVtc_Enable(&Vtc);
    XVtc_EnableGenerator(&Vtc);
    XVtc_RegUpdateEnable(&Vtc);

    xil_printf("VTC mode: %s, Pixel clock: %lu Hz\r\n",
               XVidC_GetVideoModeStr(TARGET_VIDEO_MODE),
               (u32)XVidC_GetPixelClockHzByVmId(TARGET_VIDEO_MODE));

    return XST_SUCCESS;
}

static int IicInit(void)
{
    XIic_Config *Cfg;
    int Status;

    Cfg = XIic_LookupConfig((u32)IIC_BASE_ADDR);
    if (Cfg == NULL) {
        xil_printf("ERR: XIic_LookupConfig failed\r\n");
        return XST_FAILURE;
    }

    Status = XIic_CfgInitialize(&Iic, Cfg, Cfg->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("ERR: XIic_CfgInitialize failed: %d\r\n", Status);
        return Status;
    }

    Status = XIic_SelfTest(&Iic);
    if (Status != XST_SUCCESS) {
        xil_printf("ERR: XIic_SelfTest failed: %d\r\n", Status);
        return Status;
    }

    return XST_SUCCESS;
}

static int IicWriteReg(u8 DevAddr8, u16 Reg, u8 Data, int RegAddr2Byte)
{
    u8 Buf[3];
    u8 Dev7;
    int Len;
    int Sent;

    Dev7 = (u8)(DevAddr8 >> 1);

    if (RegAddr2Byte != 0) {
        Buf[0] = (u8)((Reg >> 8) & 0xFFU);
        Buf[1] = (u8)(Reg & 0xFFU);
        Buf[2] = Data;
        Len = 3;
    } else {
        Buf[0] = (u8)(Reg & 0xFFU);
        Buf[1] = Data;
        Len = 2;
    }

    Sent = XIic_Send(Iic.BaseAddress, Dev7, Buf, Len, XIIC_STOP);
    if (Sent != Len) {
        xil_printf("ERR: I2C send mismatch (sent=%d exp=%d dev7=0x%02x)\r\n",
                   Sent, Len, Dev7);
        return XST_FAILURE;
    }

    usleep(1000);
    return XST_SUCCESS;
}

static int HdmiInitFromLut(int RegAddr2Byte)
{
    int Status;
    int i;

    for (i = 0; ; ++i) {
        if (HdmiLut[i].dev_addr8 == 0xFFU) {
            return XST_SUCCESS;
        }

        Status = IicWriteReg(HdmiLut[i].dev_addr8,
                             HdmiLut[i].reg_addr,
                             HdmiLut[i].reg_data,
                             RegAddr2Byte);
        if (Status != XST_SUCCESS) {
            xil_printf("ERR: HDMI LUT failed at idx=%d dev8=0x%02x reg=0x%04x\r\n",
                       i, HdmiLut[i].dev_addr8, HdmiLut[i].reg_addr);
            return XST_FAILURE;
        }
    }
}

static int ConfigureVdmaChannel(XAxiVdma *InstancePtr, u16 Direction,
                                int EnableSync, int FrameDelay, int PointNum,
                                int UseInternalGenLock, int FsyncSource,
                                const char *Name)
{
    XAxiVdma_DmaSetup DmaCfg;
    int Status;

    (void)memset(&DmaCfg, 0, sizeof(DmaCfg));

    DmaCfg.VertSizeInput = (int)ACTIVE_HEIGHT;
    DmaCfg.HoriSizeInput = (int)STRIDE_BYTES;
    DmaCfg.Stride = (int)STRIDE_BYTES;
    DmaCfg.FrameDelay = FrameDelay;
    DmaCfg.EnableCircularBuf = 1;
    DmaCfg.EnableSync = EnableSync;
    DmaCfg.PointNum = PointNum;
    DmaCfg.EnableFrameCounter = 0;
    DmaCfg.FixedFrameStoreAddr = 0;
    DmaCfg.GenLockRepeat = 1;

    Status = XAxiVdma_SetFrmStore(InstancePtr, FRAME_COUNT, Direction);
    if ((Status != XST_SUCCESS) && (Status != XST_NO_FEATURE)) {
        xil_printf("ERROR: %s SetFrmStore failed (%d)\r\n", Name, Status);
        return XST_FAILURE;
    }

    if (FsyncSource >= 0) {
        Status = XAxiVdma_FsyncSrcSelect(InstancePtr, (u32)FsyncSource, Direction);
        if (Status != XST_SUCCESS) {
            xil_printf("ERROR: %s fsync source select failed (%d)\r\n", Name, Status);
            return XST_FAILURE;
        }
    }

    Status = XAxiVdma_DmaConfig(InstancePtr, Direction, &DmaCfg);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: %s DmaConfig failed (%d)\r\n", Name, Status);
        return XST_FAILURE;
    }

    if (UseInternalGenLock != 0) {
        Status = XAxiVdma_GenLockSourceSelect(InstancePtr,
                                              XAXIVDMA_INTERNAL_GENLOCK,
                                              Direction);
        if (Status != XST_SUCCESS) {
            xil_printf("ERROR: %s internal genlock select failed (%d)\r\n", Name, Status);
            return XST_FAILURE;
        }
    }

    Status = XAxiVdma_DmaSetBufferAddr(InstancePtr, Direction, FrameAddr);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: %s DmaSetBufferAddr failed (%d)\r\n", Name, Status);
        return XST_FAILURE;
    }

    xil_printf("%s configured: sync=%d frame_delay=%d point=%d fsync_src=%d\r\n",
               Name, EnableSync, FrameDelay, PointNum, FsyncSource);
    return XST_SUCCESS;
}

static int StartVdmaChannel(XAxiVdma *InstancePtr, u16 Direction, const char *Name)
{
    int Status;

    Status = XAxiVdma_DmaStart(InstancePtr, Direction);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: %s DmaStart failed (%d)\r\n", Name, Status);
        return XST_FAILURE;
    }

    xil_printf("%s started\r\n", Name);
    return XST_SUCCESS;
}

static void PrintDmaChannelState(XAxiVdma *InstancePtr, u16 Direction, const char *Name)
{
    UINTPTR ChanBase;
    u32 Cr;
    u32 Sr;
    int Errors;

    ChanBase = InstancePtr->BaseAddr +
               ((Direction == XAXIVDMA_READ) ? XAXIVDMA_TX_OFFSET
                                             : XAXIVDMA_RX_OFFSET);
    Cr = XAxiVdma_ReadReg(ChanBase, XAXIVDMA_CR_OFFSET);
    Sr = XAxiVdma_ReadReg(ChanBase, XAXIVDMA_SR_OFFSET);
    Errors = XAxiVdma_GetDmaChannelErrors(InstancePtr, Direction);

    xil_printf("%s CR=0x%08lx SR=0x%08lx ERR=0x%08x {halted=%lu idle=%lu fsync_src=%lu sync_en=%lu}\r\n",
               Name,
               Cr,
               Sr,
               (u32)Errors,
               (Sr & XAXIVDMA_SR_HALTED_MASK) ? 1U : 0U,
               (Sr & XAXIVDMA_SR_IDLE_MASK) ? 1U : 0U,
               (Cr & XAXIVDMA_CR_FSYNC_SRC_MASK) >> 5,
               (Cr & XAXIVDMA_CR_SYNC_EN_MASK) ? 1U : 0U);
}

static void PrintPipelineState(const char *StepLabel)
{
    u32 WriteFrame;
    u32 ReadFrame;

    WriteFrame = XAxiVdma_CurrFrameStore(&Vdma, XAXIVDMA_WRITE);
    ReadFrame = XAxiVdma_CurrFrameStore(&Vdma, XAXIVDMA_READ);

    xil_printf("[%s] VDMA.S2MM=%lu VDMA.MM2S=%lu\r\n",
               StepLabel, WriteFrame, ReadFrame);
    PrintDmaChannelState(&Vdma, XAXIVDMA_WRITE, "  VDMA S2MM");
    PrintDmaChannelState(&Vdma, XAXIVDMA_READ, "  VDMA MM2S");
}

static void DumpFrameSample(void)
{
    u32 CompletedFrame;
    volatile u32 *Words;

    CompletedFrame = (XAxiVdma_CurrFrameStore(&Vdma, XAXIVDMA_WRITE) +
                      FRAME_COUNT - 1U) % FRAME_COUNT;
    Xil_DCacheInvalidateRange(FrameAddr[CompletedFrame], 32U);
    Words = (volatile u32 *)(UINTPTR)FrameAddr[CompletedFrame];

    xil_printf("[Sample] frame=%lu addr=0x%08lx words=%08lx %08lx %08lx %08lx\r\n",
               CompletedFrame,
               (u32)FrameAddr[CompletedFrame],
               Words[0], Words[1], Words[2], Words[3]);
}

int main(void)
{
    int Status;

    xil_printf("\r\n==== PCIe XDMA -> VDMA -> DDR -> HDMI Test ====\r\n");
    xil_printf("CPU clk: %lu Hz\r\n", (u32)XPAR_CPU_CORE_CLOCK_FREQ_HZ);
    xil_printf("Mode: %s, %lux%lu, stride=%lu bytes, frame=%lu bytes\r\n",
               XVidC_GetVideoModeStr(TARGET_VIDEO_MODE),
               (u32)ACTIVE_WIDTH,
               (u32)ACTIVE_HEIGHT,
               (u32)STRIDE_BYTES,
               (u32)FRAME_SIZE_BYTES);
    xil_printf("IMPORTANT: verify XDMA H2C frame-start behavior on the ILA before trusting S2MM capture.\r\n");

    Status = IicInit();
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Status = InitVideoLockMonitor(&VideoLockMonitor);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    PrintVideoLockMonitor("Boot");

    BuildFrameAddresses();
    ClearFrameRing();

    Status = InitPixelUnpack(&PixelUnpack);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    xil_printf("Pixel unpack configured (mode=1)\r\n");

    Status = InitColorConvert(&ColorConvert);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    xil_printf("Color convert configured (identity matrix)\r\n");

    Status = LookupAndInitVdma(&Vdma);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Status = ConfigureVdmaChannel(&Vdma,
                                  XAXIVDMA_WRITE,
                                  1,
                                  0,
                                  DISPLAY_POINT_NUM,
                                  1,
                                  PCIE_CAPTURE_FSYNC_SOURCE,
                                  "VDMA S2MM");
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Status = ConfigureVdmaChannel(&Vdma,
                                  XAXIVDMA_READ,
                                  1,
                                  DISPLAY_READ_FRAME_DELAY,
                                  DISPLAY_POINT_NUM,
                                  1,
                                  -1,
                                  "VDMA MM2S");
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Status = StartVdmaChannel(&Vdma, XAXIVDMA_WRITE, "VDMA S2MM");
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Status = StartVdmaChannel(&Vdma, XAXIVDMA_READ, "VDMA MM2S");
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    PrintPipelineState("After DMA start");

    sleep(1);
    Status = HdmiInitFromLut(0);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    sleep(1);
    Status = LookupAndInitVtc(&Vtc);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    Status = ConfigureVtcGenerator();
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    PrintVideoLockMonitor("After VTC");

    while (1) {
        PrintPipelineState("Runtime");
        DumpFrameSample();
        PrintVideoLockMonitor("Runtime");
        sleep(1);
    }

    return XST_SUCCESS;
}
