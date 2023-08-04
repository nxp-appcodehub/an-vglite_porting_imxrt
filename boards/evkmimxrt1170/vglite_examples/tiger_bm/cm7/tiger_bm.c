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
#include "tiger_paths.h"
#include "display_support.h"
/*-----------------------------------------------------------*/
#include "vg_lite.h"

#include "fsl_soc_src.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_BUFFER_COUNT 2
#define DEFAULT_SIZE     256.0f;

/* LCD input frame buffer is RGB565, converted by PXP. */
#define DEMO_TIGER_BUFFER_BPP DEMO_BUFFER_BYTE_PER_PIXEL

#define DEMO_TIGER_BUFFER_COUNT 3

#define VGLITE_GetRenderTarget(x) &vg_buffer[x]


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

static int zoomOut    = 0;
static int scaleCount = 0;
static vg_lite_matrix_t matrix;

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
    
    PRINTF("Tiger demo using baremetal VGLite driver.\r\n");
    DEMO_vglite();
    for (;;) ;
}

static void cleanup(void)
{
    uint8_t i;
    for (i = 0; i < pathCount; i++)
    {
        vg_lite_clear_path(&path[i]);
    }

    vg_lite_close();
}

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fb_width, fb_height;

    // Initialize the draw.
    error = vg_lite_init(TW, TH);
    if (error)
    {
        PRINTF("vg_lite engine init failed: vg_lite_init() returned error %d\r\n", error);
        cleanup();
        return error;
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
    vg_lite_translate(fb_width / 2 - 20 * fb_width / 640.0f, fb_height / 2 - 100 * fb_height / 480.0f, &matrix);
    vg_lite_scale(4, 4, &matrix);
    vg_lite_scale(fb_width / 640.0f, fb_height / 480.0f, &matrix);

    return error;
}

void animateTiger()
{
    if (zoomOut)
    {
        vg_lite_scale(1.25, 1.25, &matrix);
        if (0 == --scaleCount)
            zoomOut = 0;
    }
    else
    {
        vg_lite_scale(0.8, 0.8, &matrix);
        if (5 == ++scaleCount)
            zoomOut = 1;
    }

    vg_lite_rotate(5, &matrix);
}

static void redraw()
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    uint8_t count;
    vg_lite_buffer_t *rt = VGLITE_GetRenderTarget(s_lcdActiveFbIdx);
    if (rt == NULL)
    {
        PRINTF("vg_lite_get_renderTarget error\r\n");
        while (1)
            ;
    }

    // Draw the path using the matrix.
    vg_lite_clear(rt, NULL, 0xFFFFFFFF);
    for (count = 0; count < pathCount; count++)
    {
        error = vg_lite_draw(rt, &path[count], VG_LITE_FILL_EVEN_ODD, &matrix, VG_LITE_BLEND_NONE, color_data[count]);
        if (error)
        {
            PRINTF("vg_lite_draw() returned error %d\r\n", error);
            cleanup();
            return;
        }
    }
    
    vg_lite_flush();

    animateTiger();

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
