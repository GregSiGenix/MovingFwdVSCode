/*********************************************************************
 *                   (c) SEGGER Microcontroller GmbH                  *
 *                        The Embedded Experts                        *
 **********************************************************************
 *                                                                    *
 *       (c) 2003 - 2022     SEGGER Microcontroller GmbH              *
 *                                                                    *
 *       www.segger.com     Support: www.segger.com/ticket            *
 *                                                                    *
 **********************************************************************
 *                                                                    *
 *       emUSB-Host * USB Host stack for embedded applications        *
 *                                                                    *
 *       Please note: Knowledge of this file may under no             *
 *       circumstances be used to write a similar product.            *
 *       Thank you for your fairness !                                *
 *                                                                    *
 **********************************************************************
 *                                                                    *
 *       emUSB-Host version: V2.36.1                                  *
 *                                                                    *
 **********************************************************************
 ----------------------------------------------------------------------
 File        : USBH_OS_FreeRTOS.c
 Purpose     : Kernel abstraction for the FreeRTOS RTOS.
 Do not modify to allow easy updates!
 ---------------------------END-OF-HEADER------------------------------
 */

/*********************************************************************
 *
 *       #include Section
 *
 **********************************************************************
 */

#include "USBH.h"
#include "USBH_Util.h"
#include "USBH_MEM.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include "USBH_Int.h"

/*********************************************************************
 *
 *       Type definitions
 *
 **********************************************************************
 */
struct _USBH_OS_EVENT_OBJ
{
	USBH_DLIST ListEntry;
	EventGroupHandle_t EventTask;  // Using EventGroup for signaling
};

#define GET_EVENT_OBJ_FROM_ENTRY(pListEntry) STRUCT_BASE_POINTER(pListEntry, USBH_OS_EVENT_OBJ, ListEntry)

/*********************************************************************
 *
 *       Static data
 *
 **********************************************************************
 */
static SemaphoreHandle_t _aMutex[USBH_MUTEX_COUNT];
static EventGroupHandle_t _EventNet;
static EventGroupHandle_t _EventISR;
static volatile U32 _IsrMask;
static USBH_DLIST _UserEventList;

/*********************************************************************
 *
 *       Public code
 *
 **********************************************************************
 */

/*********************************************************************
 *
 *       USBH_OS_DisableInterrupt
 *
 *  Function description
 *    Enter a critical region for the USB stack: Disables interrupts.
 */
void USBH_OS_DisableInterrupt(void)
{
	taskENTER_CRITICAL();
}

/*********************************************************************
 *
 *       USBH_OS_EnableInterrupt
 *
 *  Function description
 *    Leave a critical region for the USB stack: Enables interrupts.
 */
void USBH_OS_EnableInterrupt(void)
{
	taskEXIT_CRITICAL();
}

/*********************************************************************
 *
 *       USBH_OS_Init
 *
 *  Function description
 *    Initialize (create) all objects required for task synchronization.
 */
void USBH_OS_Init(void)
{
	unsigned i;

	// Create event groups for network and ISR events
	_EventNet = xEventGroupCreate();
	_EventISR = xEventGroupCreate();
	USBH_ASSERT(_EventNet != NULL);
	USBH_ASSERT(_EventISR != NULL);

	// Create recursive mutexes
	for (i = 0; i < SEGGER_COUNTOF(_aMutex); i++)
	{
		_aMutex[i] = xSemaphoreCreateRecursiveMutex();
		USBH_ASSERT(_aMutex[i] != NULL);
	}

	USBH_DLIST_Init(&_UserEventList);
}

/*********************************************************************
 *
 *       USBH_OS_Lock
 *
 *  Function description
 *    Locks a mutex object, guarding sections of the stack code.
 *    Mutexes are recursive.
 */
void USBH_OS_Lock(unsigned Idx)
{
#if USBH_SUPPORT_WARN
  if (Idx >= USBH_MUTEX_COUNT) {
    USBH_PANIC("OS: bad mutex index");
  }
  // FreeRTOS doesn't provide a direct way to check mutex hierarchy like embOS,
  // so this check is omitted or could be implemented with custom logic.
#endif
	xSemaphoreTakeRecursive(_aMutex[Idx], portMAX_DELAY);
}

/*********************************************************************
 *
 *       USBH_OS_Unlock
 *
 *  Function description
 *    Unlocks the mutex used by a previous call to USBH_OS_Lock().
 */
void USBH_OS_Unlock(unsigned Idx)
{
	xSemaphoreGiveRecursive(_aMutex[Idx]);
}

/*********************************************************************
 *
 *       USBH_OS_GetTime32
 *
 *  Function description
 *    Return the current system time in ms.
 */
USBH_TIME USBH_OS_GetTime32(void)
{
	return xTaskGetTickCount() * (1000 / configTICK_RATE_HZ);
}

/*********************************************************************
 *
 *       USBH_OS_Delay
 *
 *  Function description
 *    Blocks the calling task for a given time.
 */
void USBH_OS_Delay(unsigned ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms));
}

/*********************************************************************
 *
 *       USBH_OS_WaitNetEvent
 *
 *  Function description
 *    Blocks until the timeout expires or a USBH-event occurs.
 */
void USBH_OS_WaitNetEvent(unsigned ms)
{
	xEventGroupWaitBits(_EventNet, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms));
}

/*********************************************************************
 *
 *       USBH_OS_SignalNetEvent
 *
 *  Function description
 *    Wakes the USBH_MainTask() if it is waiting for an event.
 */
void USBH_OS_SignalNetEvent(void)
{
	xEventGroupSetBits(_EventNet, 0x01);
}

/*********************************************************************
 *
 *       USBH_OS_WaitISR
 *
 *  Function description
 *    Blocks until USBH_OS_SignalISR() is called (from ISR).
 */
U32 USBH_OS_WaitISR(void)
{
	U32 r;

	xEventGroupWaitBits(_EventISR, 0x01, pdTRUE, pdFALSE, portMAX_DELAY);
	taskENTER_CRITICAL();
	r = _IsrMask;
	_IsrMask = 0;
	taskEXIT_CRITICAL();
	return r;
}

/*********************************************************************
 *
 *       USBH_OS_SignalISREx
 *
 *  Function description
 *    Wakes the USBH_ISRTask(). Called from ISR.
 */
void USBH_OS_SignalISREx(U32 DevIndex)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	_IsrMask |= (1 << DevIndex);
	xEventGroupSetBitsFromISR(_EventISR, 0x01, &xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*********************************************************************
 *
 *       USBH_OS_AllocEvent
 *
 *  Function description
 *    Allocates and returns an event object.
 */
USBH_OS_EVENT_OBJ* USBH_OS_AllocEvent(void)
{
	USBH_OS_EVENT_OBJ *p;

	p = (USBH_OS_EVENT_OBJ*) USBH_TRY_MALLOC(sizeof(USBH_OS_EVENT_OBJ));
	if (p)
	{
		USBH_DLIST_Init(&p->ListEntry);
		p->EventTask = xEventGroupCreate();
		if (p->EventTask == NULL)
		{
			USBH_FREE(p);
			return NULL;
		}
		USBH_DLIST_InsertTail(&_UserEventList, &p->ListEntry);
	}
	return p;
}

/*********************************************************************
 *
 *       USBH_OS_FreeEvent
 *
 *  Function description
 *    Releases an event object.
 */
void USBH_OS_FreeEvent(USBH_OS_EVENT_OBJ *pEvent)
{
	USBH_DLIST_RemoveEntry(&pEvent->ListEntry);
	vEventGroupDelete(pEvent->EventTask);
	USBH_FREE(pEvent);
}

/*********************************************************************
 *
 *       USBH_OS_SetEvent
 *
 *  Function description
 *    Sets the state of the specified event object to signalled.
 */
void USBH_OS_SetEvent(USBH_OS_EVENT_OBJ *pEvent)
{
	xEventGroupSetBits(pEvent->EventTask, 0x01);
}

/*********************************************************************
 *
 *       USBH_OS_ResetEvent
 *
 *  Function description
 *    Sets the state of the specified event object to non-signalled.
 */
void USBH_OS_ResetEvent(USBH_OS_EVENT_OBJ *pEvent)
{
	xEventGroupClearBits(pEvent->EventTask, 0x01);
}

/*********************************************************************
 *
 *       USBH_OS_WaitEvent
 *
 *  Function description
 *    Wait for the specific event.
 */
void USBH_OS_WaitEvent(USBH_OS_EVENT_OBJ *pEvent)
{
	xEventGroupWaitBits(pEvent->EventTask, 0x01, pdTRUE, pdFALSE,
			portMAX_DELAY);
}

/*********************************************************************
 *
 *       USBH_OS_WaitEventTimed
 *
 *  Function description
 *    Wait for the specific event within a given timeout.
 */
int USBH_OS_WaitEventTimed(USBH_OS_EVENT_OBJ *pEvent, U32 MilliSeconds)
{
	EventBits_t bits;
	bits = xEventGroupWaitBits(pEvent->EventTask, 0x01, pdTRUE, pdFALSE,
			pdMS_TO_TICKS(MilliSeconds));
	return (bits & 0x01) ? USBH_OS_EVENT_SIGNALED : USBH_OS_EVENT_TIMEOUT;
}

/*********************************************************************
 *
 *       USBH_OS_DeInit
 *
 *  Function description
 *    Deletes all objects required for task synchronization.
 */
void USBH_OS_DeInit(void)
{
	USBH_DLIST *pListHead;
	USBH_DLIST *pEntry;
	unsigned i;

	pListHead = &_UserEventList;
	pEntry = USBH_DLIST_GetNext(pListHead);
	while (pListHead != pEntry)
	{
		USBH_OS_EVENT_OBJ *pEvent;
		pEvent = GET_EVENT_OBJ_FROM_ENTRY(pEntry);
		vEventGroupDelete(pEvent->EventTask);
		pEntry = USBH_DLIST_GetNext(pEntry);
		USBH_DLIST_RemoveEntry(&pEvent->ListEntry);
		USBH_FREE(pEvent);
	}
	vEventGroupDelete(_EventNet);
	vEventGroupDelete(_EventISR);
	for (i = 0; i < SEGGER_COUNTOF(_aMutex); i++)
	{
		vSemaphoreDelete(_aMutex[i]);
	}
}

/*************************** End of file ****************************/
