#ifndef _VG_LITE_OS_H
#define _VG_LITE_OS_H

#include <stdint.h>

typedef uint32_t  vg_lite_os_async_event_t; /* Not used, just for API consistent*/
/*!
@brief  Set the value in a task’s thread local storage array.
*/
int32_t vg_lite_os_set_tls(void* tls);

/*!
@brief  Get the current task’s thread local storage array.
*/
void * vg_lite_os_get_tls( );

/*!
@brief  Memory allocate.
*/
void * vg_lite_os_malloc(uint32_t size);

/*!
@brief  Memory free.
*/
void vg_lite_os_free(void * memory);

/*!
@brief  Reset the value in a task’s thread local storage array.
*/
void vg_lite_os_reset_tls();


/*!
@brief  sleep a number of milliseconds.
*/
void vg_lite_os_sleep(uint32_t msec);

/*!
@brief  initialize the os parameters.
*/
int32_t vg_lite_os_initialize();

/*!
@brief  deinitialize the os parameters.
*/
void vg_lite_os_deinitialize();

/*!
@brief  Mutex semaphore take.
*/
int32_t vg_lite_os_lock();

/*!
@brief  Mutex semaphore give.
*/
int32_t vg_lite_os_unlock();

/*!
@brief  Submit the current command buffer to the command queue.
*/
int32_t vg_lite_os_submit(uint32_t context, uint32_t physical, uint32_t offset, uint32_t size, vg_lite_os_async_event_t *event);

/*!
@brief  Wait for the current command buffer to be executed.
*/
int32_t vg_lite_os_wait(uint32_t timeout, vg_lite_os_async_event_t *event);

/*!
@brief  IRQ Handler.
*/
void vg_lite_os_IRQHandler();

/*!
@brief
*/
int8_t vg_lite_os_query_context_switch(uint32_t context);

#endif
