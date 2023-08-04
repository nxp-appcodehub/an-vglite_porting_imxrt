/*
 * Copyright 2019, 2021 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS kernel includes. */
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "board.h"
#include "vglite_support.h"
#include "display_support.h"
/*-----------------------------------------------------------*/
#include "vg_lite.h"
#include "Elm.h"

#include "clock_analog.h"
#include "hour_needle.h"
#include "minute_needle.h"

#include "fsl_soc_src.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_BUFFER_COUNT 3
#define DEFAULT_SIZE     256.0f;

/* LCD input frame buffer is RGB565, converted by PXP. */
#define DEMO_TIGER_BUFFER_BPP DEMO_BUFFER_BYTE_PER_PIXEL

#define DEMO_TIGER_BUFFER_COUNT 3

#define VGLITE_GetRenderTarget(x) &vg_buffer[x]

typedef struct elm_render_buffer
{
    ElmBuffer handle;
    vg_lite_buffer_t *buffer;
} ElmRenderBuffer;


/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void DEMO_vglite(void);
static void DEMO_BufferSwitchOffCallback(void *param, void *switchOffBuffer);
static void DEMO_InitDisplay(void);


/*******************************************************************************
 * Variables
 ******************************************************************************/
AT_NONCACHEABLE_SECTION_ALIGN(
    static uint8_t s_lcdBuffer[DEMO_TIGER_BUFFER_COUNT][DEMO_BUFFER_HEIGHT][DEMO_BUFFER_WIDTH * DEMO_TIGER_BUFFER_BPP],
    FRAME_BUFFER_ALIGN);

static vg_lite_buffer_t vg_buffer[DEMO_TIGER_BUFFER_COUNT];
/*
 * When new frame buffer sent to display, it might not be shown immediately.
 * Application could use callback to get new frame shown notification, at the
 * same time, when this flag is set, application could write to the older
 * frame buffer.
 */
static volatile bool s_newFrameShown = false;
static dc_fb_info_t fbInfo;
static uint8_t s_lcdActiveFbIdx;


static vg_lite_matrix_t matrix;

static ElmHandle analogClockHandle  = ELM_NULL_HANDLE;
static ElmHandle hourNeedleHandle   = ELM_NULL_HANDLE;
static ElmHandle minuteNeedleHandle = ELM_NULL_HANDLE;
static ElmRenderBuffer elmFB[APP_BUFFER_COUNT];

extern unsigned int ClockAnalog_evo_len;
extern unsigned char ClockAnalog_evo[];

extern unsigned int HourNeedle_evo_len;
extern unsigned char HourNeedle_evo[];

extern unsigned int MinuteNeedle_evo_len;
extern unsigned char MinuteNeedle_evo[];


#if (CUSTOM_VGLITE_MEMORY_CONFIG != 1)
#error "Application must be compiled with CUSTOM_VGLITE_MEMORY_CONFIG=1"
#else
#define VGLITE_COMMAND_BUFFER_SZ (128 * 1024)
/* On RT595S */
#if defined(CPU_MIMXRT595SFFOC_cm33)
#define VGLITE_HEAP_SZ 0x400000 /* 4 MB */
/* On RT1170 */
#elif defined(CPU_MIMXRT1176DVMAA_cm7) || defined(CPU_MIMXRT1166DVM6A_cm7)
#define VGLITE_HEAP_SZ 8912896 /* 8.5 MB */
#else
#error "Unsupported CPU !"
#endif
#if (720 * 1280 == (DEMO_PANEL_WIDTH) * (DEMO_PANEL_HEIGHT))
#define TW 720
/* On RT595S */
#if defined(CPU_MIMXRT595SFFOC_cm33)
/* Tessellation window = 720 x 640 */
#define TH 640
/* On RT1170 */
#elif defined(CPU_MIMXRT1176DVMAA_cm7) || defined(CPU_MIMXRT1166DVM6A_cm7)
/* Tessellation window = 720 x 1280 */
#define TH 1280
#else
#error "Unsupported CPU !"
#endif
/* Panel RM67162. Supported only by platform RT595S. */
#elif (400 * 400 == (DEMO_PANEL_WIDTH) * (DEMO_PANEL_HEIGHT))
/* Tessellation window = 400 x 400 */
#define TW 400
#define TH 400
#else
/* Tessellation window = 256 x 256 */
#define TW 256
#define TH 256
#endif
/* Allocate the heap and set the command buffer(s) size */
AT_NONCACHEABLE_SECTION_ALIGN(uint8_t vglite_heap[VGLITE_HEAP_SZ], 64);

void *vglite_heap_base        = &vglite_heap;
uint32_t vglite_heap_size     = VGLITE_HEAP_SZ;
#endif

/*******************************************************************************
 * Code
 ******************************************************************************/
static void BOARD_ResetDisplayMix(void)
{
    /*
     * Reset the displaymix, otherwise during debugging, the
     * debugger may not reset the display, then the behavior
     * is not right.
     */
    SRC_AssertSliceSoftwareReset(SRC, kSRC_DisplaySlice);
    while (kSRC_SliceResetInProcess == SRC_GetSliceResetState(SRC, kSRC_DisplaySlice))
    {
    }
}

static vg_lite_buffer_format_t video_format_to_vglite(video_pixel_format_t format)
{
    vg_lite_buffer_format_t fmt;
    switch (format)
    {
        case kVIDEO_PixelFormatRGB565:
            fmt = VG_LITE_BGR565;
            break;

        case kVIDEO_PixelFormatBGR565:
            fmt = VG_LITE_RGB565;
            break;

        case kVIDEO_PixelFormatXRGB8888:
            fmt = VG_LITE_BGRX8888;
            break;

        default:
            fmt = VG_LITE_RGB565;
            break;
    }

    return fmt;
}

static ELM_BUFFER_FORMAT _buffer_format_to_Elm(vg_lite_buffer_format_t format)
{
    switch (format)
    {
        case VG_LITE_RGB565:
            return ELM_BUFFER_FORMAT_RGB565;
            break;
        case VG_LITE_BGR565:
            return ELM_BUFFER_FORMAT_BGR565;
            break;
        case VG_LITE_BGRX8888:
            return ELM_BUFFER_FORMAT_BGRX8888;
            break;
        default:
            return ELM_BUFFER_FORMAT_RGBA8888;
            break;
    }
}

static void DEMO_BufferSwitchOffCallback(void *param, void *switchOffBuffer)
{
    s_newFrameShown = true;
}

static void DEMO_InitDisplay(void)
{
    status_t status;
    int i;

    BOARD_PrepareDisplayController();

    status = g_dc.ops->init(&g_dc);
    if (kStatus_Success != status)
    {
        PRINTF("Display initialization failed\r\n");
        assert(0);
    }

    g_dc.ops->getLayerDefaultConfig(&g_dc, 0, &fbInfo);
    fbInfo.pixelFormat = DEMO_BUFFER_PIXEL_FORMAT;
    fbInfo.width       = DEMO_BUFFER_WIDTH;
    fbInfo.height      = DEMO_BUFFER_HEIGHT;
    fbInfo.startX      = DEMO_BUFFER_START_X;
    fbInfo.startY      = DEMO_BUFFER_START_Y;
    fbInfo.strideBytes = DEMO_BUFFER_WIDTH * DEMO_TIGER_BUFFER_BPP;
    g_dc.ops->setLayerConfig(&g_dc, 0, &fbInfo);

    g_dc.ops->setCallback(&g_dc, 0, DEMO_BufferSwitchOffCallback, NULL);

    s_lcdActiveFbIdx = 0;
    s_newFrameShown  = false;
    g_dc.ops->setFrameBuffer(&g_dc, 0, s_lcdBuffer[s_lcdActiveFbIdx]);

    /* For the DBI interface display, application must wait for the first
     * frame buffer sent to the panel.
     */
    if ((g_dc.ops->getProperty(&g_dc) & kDC_FB_ReserveFrameBuffer) == 0)
    {
        while (s_newFrameShown == false)
        {
        }
    }

    s_newFrameShown = true;

    g_dc.ops->enableLayer(&g_dc, 0);
    
    /* Wrap the framebuffer to VGLite render buffer */
    for (i = 0; i < DEMO_TIGER_BUFFER_COUNT; i++)
    {
        vg_buffer[i].memory = (void*)s_lcdBuffer[i];
        vg_buffer[i].address = (uint32_t)s_lcdBuffer[i];
        vg_buffer[i].width = DEMO_BUFFER_WIDTH;
        vg_buffer[i].height = DEMO_BUFFER_HEIGHT;
        vg_buffer[i].stride = DEMO_BUFFER_WIDTH * DEMO_TIGER_BUFFER_BPP;
        vg_buffer[i].format = video_format_to_vglite(DEMO_BUFFER_PIXEL_FORMAT);          
    }
}

int main(void)
{
    /* Init board hardware. */
    BOARD_ConfigMPU();
    BOARD_BootClockRUN();
    BOARD_ResetDisplayMix();
    BOARD_InitLpuartPins();
    BOARD_InitMipiPanelPins();
    BOARD_InitDebugConsole();

    DEMO_InitDisplay();
    
    PRINTF("Clock demo using baremetal VGLite/Elementary driver.\r\n");
    DEMO_vglite();
    for (;;) ;
}

static void cleanup(void)
{
    vg_lite_close();
}

static int load_clock()
{
    if (ClockAnalog_evo_len != 0)
    {
        analogClockHandle = ElmCreateObjectFromData(ELM_OBJECT_TYPE_EGO, (void *)ClockAnalog_evo, ClockAnalog_evo_len);
    }

    return (analogClockHandle != ELM_NULL_HANDLE);
}

static int load_hour()
{
    if (HourNeedle_evo_len != 0)
    {
        hourNeedleHandle = ElmCreateObjectFromData(ELM_OBJECT_TYPE_EGO, (void *)HourNeedle_evo, HourNeedle_evo_len);
    }

    return (hourNeedleHandle != ELM_NULL_HANDLE);
}

static int load_minute()
{
    if (MinuteNeedle_evo_len != 0)
    {
        minuteNeedleHandle =
            ElmCreateObjectFromData(ELM_OBJECT_TYPE_EGO, (void *)MinuteNeedle_evo, MinuteNeedle_evo_len);
    }

    return (minuteNeedleHandle != ELM_NULL_HANDLE);
}

static int load_texture()
{
    int ret = 0;
    ret     = load_clock();
    if (ret <= 0)
    {
        PRINTF("load_clock failed\r\n");
        return ret;
    }
    ret = load_hour();
    if (ret <= 0)
    {
        PRINTF("load_hour failed\r\n");
        return ret;
    }
    ret = load_minute();
    if (ret <= 0)
    {
        PRINTF("load_minute failed\r\n");
    }
    return ret;
}

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int ret               = 0;
    int fb_width, fb_height;

    // Initialize the draw.
    ret = ElmInitialize(DEFAULT_VG_LITE_TW_WIDTH, DEFAULT_VG_LITE_TW_HEIGHT);
    if (!ret)
    {
        PRINTF("ElmInitialize failed\r\n");
        cleanup();
        return VG_LITE_OUT_OF_MEMORY;
    }

    // Set GPU command buffer size for this drawing task.
    error = vg_lite_set_command_buffer_size(VGLITE_COMMAND_BUFFER_SZ);
    if (error)
    {
        PRINTF("vg_lite_set_command_buffer_size() returned error %d\r\n", error);
        cleanup();
        return error;
    }

    // Setup a scale at center of buffer.
    fb_width  = DEMO_BUFFER_WIDTH;
    fb_height = DEMO_BUFFER_HEIGHT;
    vg_lite_identity(&matrix);
    vg_lite_translate(fb_width / 2.0f, fb_height/ 2.0f, &matrix);
    // load the texture;
    ret = load_texture();
    if (ret <= 0)
    {
        PRINTF("load_texture error\r\n");
        return VG_LITE_OUT_OF_MEMORY;
    }

    return error;
}

static ElmBuffer get_elm_buffer(vg_lite_buffer_t *buffer)
{
    for (int i = 0; i < APP_BUFFER_COUNT; i++)
    {
        if (elmFB[i].buffer == NULL)
        {
            elmFB[i].buffer = buffer;
            elmFB[i].handle = ElmWrapBuffer(buffer->width, buffer->height, buffer->stride, buffer->memory,
                                            buffer->address, _buffer_format_to_Elm(buffer->format));
            vg_lite_clear(buffer, NULL, 0x0);
            return elmFB[i].handle;
        }
        if (elmFB[i].buffer == buffer)
            return elmFB[i].handle;
    }
    return 0;
}
static int render(vg_lite_buffer_t *buffer, ElmHandle object)
{
    int status                = 0;
    ElmBuffer elmRenderBuffer = get_elm_buffer(buffer);
    status                    = ElmDraw(elmRenderBuffer, object);
    if (!status)
    {
        status = -1;
        return status;
    }
    ElmFinish();
    return status;
}

static void redraw()
{
    int status            = 0;
    
    vg_lite_buffer_t *rt = VGLITE_GetRenderTarget(s_lcdActiveFbIdx);
    if (rt == NULL)
    {
        PRINTF("vg_lite_get_renderTarget error\r\n");
        while (1)
            ;
    }
    static float angle = 0;

    // Draw the path using the matrix.
    status = render(rt, analogClockHandle);
    if (status == -1)
    {
        PRINTF("ELM Render analogClockHandle Failed");
        return;
    }

    ElmReset(hourNeedleHandle, ELM_PROP_TRANSFER_BIT);
    ElmTransfer(hourNeedleHandle, 200.0, 200.0);
    ElmRotate(hourNeedleHandle, angle);

    status = render(rt, hourNeedleHandle);
    if (status == -1)
    {
        PRINTF("ELM Render hourNeedleHandle Failed");
    }

    ElmReset(minuteNeedleHandle, ELM_PROP_TRANSFER_BIT);
    ElmTransfer(minuteNeedleHandle, 200.0, 200.0);
    ElmRotate(minuteNeedleHandle, -angle);

    status = render(rt, minuteNeedleHandle);
    if (status == -1)
    {
        PRINTF("ELM Render minuteNeedleHandle Failed");
    }

    angle += 0.5;

    vg_lite_flush();

    return;
}

static void DEMO_vglite(void)
{
    status_t status;
    uint32_t idle;
    vg_lite_error_t error;

    status = BOARD_PrepareVGLiteController();
    if (status != kStatus_Success)
    {
        PRINTF("Prepare VGlite contolor error\r\n");
        while (1)
            ;
    }

    error = init_vg_lite();
    if (error)
    {
        PRINTF("init_vg_lite failed: init_vg_lite() returned error %d\r\n", error);
        while (1)
            ;
    }

    while (1)
    {
        s_lcdActiveFbIdx++;
        s_lcdActiveFbIdx = s_lcdActiveFbIdx % 3;
        
        redraw();
        
        /* Wait for the previous frame buffer is shown, and there is available
           inactive buffer to fill. */
        while (s_newFrameShown == false)
        {
        }

        /* Show the new frame. */
        s_newFrameShown = false;
        idle = 0;
        while (!idle)
        {
            vg_lite_query_idle(&idle);
        }
        
        g_dc.ops->setFrameBuffer(&g_dc, 0, s_lcdBuffer[s_lcdActiveFbIdx]);
        
    }
}
