#include "vg_lite_os.h"
#include "vg_lite_hw.h"
#include "vg_lite_hal.h"
#include "fsl_common.h"

/* If bit31 is activated this indicates a bus error */
#define IS_AXI_BUS_ERR(x) ((x)&(1U << 31))

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

static volatile uint32_t int_done;
static uint32_t curContext;
static void* pTLS;

void __attribute__((weak)) vg_lite_bus_error_handler()
{
    /*
     * Default implementation of the bus error handler does nothing. Application
     * should override this handler if it requires to be notified when a bus
     * error event occurs.
     */
     return;
}


int32_t vg_lite_os_set_tls(void* tls)
{
    if(tls == NULL)
        return VG_LITE_INVALID_ARGUMENT;

    pTLS = tls;
    return VG_LITE_SUCCESS;
}

void * vg_lite_os_get_tls( )
{
    return pTLS;
}

void * vg_lite_os_malloc(uint32_t size)
{
    return malloc(size);
}

void vg_lite_os_free(void * memory)
{
    free(memory);
}

void vg_lite_os_reset_tls()
{
    pTLS = (void*)0;
}

void vg_lite_os_sleep(uint32_t msec)
{
    SDK_DelayAtLeastUs(msec * 1000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
}

int32_t vg_lite_os_initialize(void)
{
    int_done = TRUE;
    return VG_LITE_SUCCESS;
}

void vg_lite_os_deinitialize(void)
{
    int_done = FALSE;
}

int32_t vg_lite_os_lock()
{
    return VG_LITE_SUCCESS;
}

int32_t vg_lite_os_unlock()
{
    return VG_LITE_SUCCESS;
}

int32_t vg_lite_os_submit(uint32_t context, uint32_t physical, uint32_t offset, uint32_t size, vg_lite_os_async_event_t *event)
{
    curContext = context;
    
    /* Wait previous command done */
    while(!int_done);
    
     /* Clear interrupt done flag */
    int_done = FALSE;
    
    vg_lite_hal_poke(VG_LITE_HW_CMDBUF_ADDRESS, physical + offset);
    vg_lite_hal_poke(VG_LITE_HW_CMDBUF_SIZE, (size +7)/8 );

    return VG_LITE_SUCCESS;
}

int32_t vg_lite_os_wait(uint32_t timeout, vg_lite_os_async_event_t *event)
{
    while(!int_done);
    return VG_LITE_SUCCESS;
}

void vg_lite_os_IRQHandler(void)
{
    uint32_t flags = vg_lite_hal_peek(VG_LITE_INTR_STATUS);

    if (flags) {
        /* Set interrupt done flags. */
        int_done = TRUE;
        if (IS_AXI_BUS_ERR(flags))
        {
            vg_lite_bus_error_handler();
        }
    }
}

int8_t vg_lite_os_query_context_switch(uint32_t context)
{
   if(!curContext || curContext == context)
        return FALSE;
    return TRUE;
}
