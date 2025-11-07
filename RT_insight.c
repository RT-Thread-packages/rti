#include "RT_insight.h"

/* ============================================================
 *                Hardware / System Configuration
 * ============================================================ */
// #define BOARD_SYSTICK_FREQ_IN_HZ         (168000000ULL) /**< CPU Systick frequency */
#define RT_INSIGHT_TUNNEL_ID             (0x52544953UL) /**< Unique ID for RT Insight tunnel ("RTIS") */
#define Frame_Header_Reserve_Value       (0x55AA0000UL) /**< Frame header reserved pattern */
#define SysTick_IRQ_ID                   (0x0000000FUL) /**< SysTick IRQ ID */
#define SysTick_Cnt_Register             (0xE000E018UL) /**< SysTick current count register */
#define SysTick_Reload_Register          (0xE000E014UL) /**< SysTick reload value register */
#define Interrupt_Control_State_Register (0xE000ED04UL) /**< Interrupt control/state register */

#define DWT_CYCCNT      (0xE0001004UL) /**< DWT cycle counter register */
#define DWT_CTRL        (0xE0001000UL) /**< DWT control register */
#define CoreDebug_DEMCR (0xE000EDFCUL) /**< Debug control register */

#define DWT_CTRL_CYCCNTENA_Msk     (1UL << 0)  /**< Enable cycle counter */
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24) /**< Enable DWT & ITM */

#define TEMP_BUFFER_SIZE 64 /**< The maximum number of entries that can be stored in the temporary buffer */

/* ============================================================
 *                       Global Variables
 * ============================================================ */
rt_tick_t Time_Stamp_Tick;    /**< Current OS tick count */
uint64_t  Time_Stamp_ns;      /**< Current time in nanoseconds */
uint64_t  Per_OSTick_ns;      /**< Duration of one OS tick (ns) */
double    Per_SysTick_Cnt_ns; /**< Duration per SysTick count (ns) */

uint32_t SysTick_Timer_Cnt;    /**< Current SysTick counter value */
uint32_t SysTick_Timer_Reload; /**< SysTick reload value */

RT_tunnel_t Tunnel;        /**< Tunnel instance for transmitting insight events */
uint32_t    Event_Num_Cnt; /**< Sequential event number counter */

uint32_t DWT_Last_Cnt = 0; /**< Last DWT counter value */
uint32_t DWT_Timer_hi = 0; /**< Overflow counter for DWT */

RT_insight_info Temp_Info_Buffer[TEMP_BUFFER_SIZE] = {0}; /**< Backup buffer for failed writes */
uint32_t        Temp_Info_Buffer_Index             = 0;   /**< Current write index of the temporary buffer */
uint32_t        Temp_Info_Buffer_Last_Index        = 0;   /**< Last successfully written index in the temporary buffer */

/* ============================================================
 *                   Time & Event Utilities
 * ============================================================ */

/**
 * @brief Initialize DWT timer for precise CPU time measurement.
 */
int CPU_TS_TmrInit(void)
{
    /* Enable DWT trace and cycle counter */
    *(volatile uint32_t *)CoreDebug_DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* Reset DWT cycle counter */
    *(volatile uint32_t *)DWT_CYCCNT = 0;

    /* Enable DWT cycle counter */
    *(volatile uint32_t *)DWT_CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    return 0;
}

/**
 * @brief Get current CPU timestamp in nanoseconds.
 * @note  Combines DWT low/high counter to form 64-bit time.
 */
static uint64_t Get_Time_Stamp_ns(void)
{
    uint32_t DWT_Cnt = *(volatile uint32_t *)DWT_CYCCNT;

    /* Handle DWT counter overflow */
    if (DWT_Cnt < DWT_Last_Cnt)
    {
        DWT_Timer_hi += 1;
    }
    DWT_Last_Cnt = DWT_Cnt;

    uint64_t full_cycle = ((uint64_t)DWT_Timer_hi << 32) | DWT_Cnt;
    uint64_t time_ns    = (uint64_t)(Per_SysTick_Cnt_ns * full_cycle);

    return time_ns;
}

/* ============================================================
 *                   Event Recording Logic
 * ============================================================ */

/**
 * @brief Record an insight event to tunnel.
 *
 * @param[in] ID         Event ID or object identifier
 * @param[in] track_type Event type (thread, ISR, object, timer)
 */
static void Event_Record(uint32_t ID, uint32_t track_type)
{
    int32_t         write_res    = 0;
    RT_insight_info Insight_Info = {0};

    /* Generate timestamp */
    Time_Stamp_ns = Get_Time_Stamp_ns();

    /* Fill event structure */
    Insight_Info.Frame_Header     = Frame_Header_Reserve_Value | (Event_Num_Cnt & 0xFFFF);
    Insight_Info.ID               = ID;
    Insight_Info.Track_Type       = track_type;
    Insight_Info.Time_Stamp_ns_lo = (uint32_t)(Time_Stamp_ns & 0xFFFFFFFF);
    Insight_Info.Time_Stamp_ns_hi = (uint32_t)(Time_Stamp_ns >> 32);

    /* Handle pending buffer (atomic update) */
    rt_base_t level             = rt_hw_interrupt_disable();
    uint32_t  Last_Index        = Temp_Info_Buffer_Last_Index;
    uint32_t  Index             = Temp_Info_Buffer_Index;
    Temp_Info_Buffer_Last_Index = Temp_Info_Buffer_Index;
    rt_hw_interrupt_enable(level);

    /* Try to flush previous buffered events first */
    if (Index != Last_Index)
    {
        uint32_t len = (Index > Last_Index) ? (Index - Last_Index) : (Index + TEMP_BUFFER_SIZE - Last_Index);

        for (uint32_t i = 0; i < len; i++)
        {
            write_res = Tunnel->write(Tunnel, (void *)&Temp_Info_Buffer[Last_Index], sizeof(RT_insight_info));
            if (write_res != sizeof(RT_insight_info))
            {
                break;
            }

            Last_Index = (Last_Index + 1) % TEMP_BUFFER_SIZE;
        }

        Temp_Info_Buffer_Last_Index = Last_Index;
    }

    /* Attempt to write the new event */
    write_res = Tunnel->write(Tunnel, (void *)&Insight_Info, sizeof(RT_insight_info));

    /* If write fails (-2 = tunnel busy), store into temp buffer */
    if ((write_res == TUNNEL_BUSY_CODE) && ((Tunnel->status & STATUS_BUFFER_Msk) == STATUS_BUFFER_AVAILABLE))
    {
        rt_memcpy(&Temp_Info_Buffer[Temp_Info_Buffer_Index], &Insight_Info, sizeof(Insight_Info));
        Temp_Info_Buffer_Index = (Temp_Info_Buffer_Index + 1) % TEMP_BUFFER_SIZE;
    }

    /* Update event counter */
    Event_Num_Cnt++;
}

/* ============================================================
 *               RT-Thread Hook Implementations
 * ============================================================ */

/**
 * @brief Scheduler hook: called during thread switch.
 *
 * @param[in] from_thread Thread being switched out
 * @param[in] to_thread   Thread being switched in
 */
static void rt_view_scheduler_hook(rt_thread_t from_thread, rt_thread_t to_thread)
{
    if (from_thread)
    {
        Event_Record((uint32_t)&from_thread->sp, THREAD_EXIT);
        Event_Record((uint32_t)&to_thread->sp, THREAD_RUN);
    }
}

/**
 * @brief Called when a thread is resumed.
 *
 * @param[in] thread Thread being resumed
 */
static void rt_view_thread_resume_hook(rt_thread_t thread)
{
    Event_Record((uint32_t)&thread->sp, THREAD_RESUME);
}

/**
 * @brief Called when a thread is suspended.
 *
 * @param[in] thread Thread being suspended
 */
static void rt_view_thread_suspend_hook(rt_thread_t thread)
{
    Event_Record((uint32_t)&thread->sp, THREAD_SUSPEND);
}

/**
 * @brief Called when entering an ISR.
 */
static void rt_view_ISR_enter_hook(void)
{
    uint32_t ISR_ID = (*(volatile uint32_t *)Interrupt_Control_State_Register) & 0xFF;
    Event_Record(ISR_ID, IRQ_ENTER);
}

/**
 * @brief Called when leaving an ISR.
 */
static void rt_view_ISR_leave_hook(void)
{
    uint32_t ISR_ID = (*(volatile uint32_t *)Interrupt_Control_State_Register) & 0xFF;
    Event_Record(ISR_ID, IRQ_EXIT);
}

/**
 * @brief Called when taking an RTOS object (semaphore, mutex, etc.)
 *
 * @param[in] object Pointer to the RTOS object
 */
static void rt_view_object_take_hook(struct rt_object *object)
{
    switch (object->type & (~RT_Object_Class_Static))
    {
    case RT_Object_Class_Semaphore:
        Event_Record((uint32_t)object->name, SEM_TAKE);
        break;
    case RT_Object_Class_Mutex:
        Event_Record((uint32_t)object->name, MUTEX_TAKE);
        break;
    case RT_Object_Class_Event:
        Event_Record((uint32_t)object->name, EVENT_TAKE);
        break;
    case RT_Object_Class_MailBox:
        Event_Record((uint32_t)object->name, MAILBOX_TAKE);
        break;
    case RT_Object_Class_MessageQueue:
        Event_Record((uint32_t)object->name, QUEUE_TAKE);
        break;
    }
}

/**
 * @brief Called when trying to take an RTOS object (non-blocking).
 *
 * @param[in] object Pointer to the RTOS object
 */
static void rt_view_object_trytake_hook(struct rt_object *object)
{
    switch (object->type & (~RT_Object_Class_Static))
    {
    case RT_Object_Class_Semaphore:
        Event_Record((uint32_t)object->name, SEM_TRYTAKE);
        break;
    case RT_Object_Class_Mutex:
        Event_Record((uint32_t)object->name, MUTEX_TRYTAKE);
        break;
    case RT_Object_Class_Event:
        Event_Record((uint32_t)object->name, EVENT_TRYTAKE);
        break;
    case RT_Object_Class_MailBox:
        Event_Record((uint32_t)object->name, MAILBOX_TRYTAKE);
        break;
    case RT_Object_Class_MessageQueue:
        Event_Record((uint32_t)object->name, QUEUE_TRYTAKE);
        break;
    }
}

/**
 * @brief Called when releasing an RTOS object.
 *
 * @param[in] object Pointer to the RTOS object
 */
static void rt_view_object_relase_hook(struct rt_object *object)
{
    switch (object->type & (~RT_Object_Class_Static))
    {
    case RT_Object_Class_Semaphore:
        Event_Record((uint32_t)object->name, SEM_RELEASE);
        break;
    case RT_Object_Class_Mutex:
        Event_Record((uint32_t)object->name, MUTEX_RELEASE);
        break;
    case RT_Object_Class_Event:
        Event_Record((uint32_t)object->name, EVENT_RELEASE);
        break;
    case RT_Object_Class_MailBox:
        Event_Record((uint32_t)object->name, MAILBOX_RELEASE);
        break;
    case RT_Object_Class_MessageQueue:
        Event_Record((uint32_t)object->name, QUEUE_RELEASE);
        break;
    }
}

/**
 * @brief Called when a timer callback starts.
 *
 * @param[in] t Timer instance
 */
static void rt_view_timer_enter_hook(rt_timer_t t)
{
    Event_Record((uint32_t)t->parent.name, TIMER_ENTER);
}

/**
 * @brief Called when a timer callback ends.
 *
 * @param[in] t Timer instance
 */
static void rt_view_timer_exit_hook(rt_timer_t t)
{
    Event_Record((uint32_t)t->parent.name, TIMER_EXIT);
}

/* ============================================================
 *                   RT Insight Initialization
 * ============================================================ */

/**
 * @brief Initialize RT Insight module and register all RT-Thread hooks.
 *
 * @retval int 0 on success, non-zero on failure.
 */
int RT_Insight_Init(void)
{
    Event_Num_Cnt        = 0;
    SysTick_Timer_Reload = *(volatile uint32_t *)SysTick_Reload_Register;
    Per_OSTick_ns        = 1000000000ULL / RT_TICK_PER_SECOND;
    Per_SysTick_Cnt_ns   = 1000000000.0 / BOARD_SYSTICK_FREQ_IN_HZ;

    /* Get a free tunnel instance */
    Tunnel = Get_Free_Tunnel();
    if (Tunnel == RT_NULL)
    {
        LOG_ERROR("Get free tunnel error");
        return -1;
    }

    /* Configure tunnel */
    if (Set_Tunnel_ID(Tunnel, RT_INSIGHT_TUNNEL_ID) != 0)
    {
        LOG_ERROR("Set tunnel ID error");
        return -1;
    }

    if (Set_Tunnel_Operation(Tunnel, tunnel_write) != 0)
    {
        LOG_ERROR("Set tunnel operation error");
        return -1;
    }

    /* Initialize timestamp source */
    CPU_TS_TmrInit();

    /* Register RT-Thread system hooks */
    rt_scheduler_sethook(rt_view_scheduler_hook);
    rt_thread_resume_sethook(rt_view_thread_resume_hook);
    rt_thread_suspend_sethook(rt_view_thread_suspend_hook);

    rt_interrupt_enter_sethook(rt_view_ISR_enter_hook);
    rt_interrupt_leave_sethook(rt_view_ISR_leave_hook);

    rt_object_trytake_sethook(rt_view_object_trytake_hook);
    rt_object_take_sethook(rt_view_object_take_hook);
    rt_object_put_sethook(rt_view_object_relase_hook);

    rt_timer_enter_sethook(rt_view_timer_enter_hook);
    rt_timer_exit_sethook(rt_view_timer_exit_hook);

    return 0;
}
MSH_CMD_EXPORT(RT_Insight_Init, RT_Insight_Init);
