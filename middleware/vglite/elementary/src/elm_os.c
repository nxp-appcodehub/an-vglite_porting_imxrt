#include "elm_os.h"
#if !defined(_BAREMETAL) && !defined(ONE_TASK_SUPPORT)
#include "FreeRTOS.h"
#include "task.h"

vg_lite_error_t elm_os_set_tls(void* tls)
{
    if(tls == NULL)
        return VG_LITE_INVALID_ARGUMENT;
    vTaskSetThreadLocalStoragePointer(NULL, 1, tls);
        return VG_LITE_SUCCESS;
}

void * elm_os_get_tls(void)
{
    return pvTaskGetThreadLocalStoragePointer(NULL, 1);
}

void elm_os_reset_tls(void)
{
    vTaskSetThreadLocalStoragePointer(NULL, 1, NULL);
}
#else

static void* pTLS;

vg_lite_error_t elm_os_set_tls(void* tls)
{
    if(tls == NULL)
        return VG_LITE_INVALID_ARGUMENT;
    pTLS = tls;
    return VG_LITE_SUCCESS;
}

void * elm_os_get_tls(void)
{
    return pTLS;
}

void elm_os_reset_tls(void)
{
    pTLS = (void*)0;
}
#endif