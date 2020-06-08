/*
 * File      : rti.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2006 - 2012, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-06-05     flybreak     first version
 */

/*
   Examples packets:
   01 F4 03 80 80 10 // Overflow packet. Data is a single U32.
                          This packet means: 500 packets lost, Timestamp is 0x40000
   02 0F 50          // ISR(15) Enter. Timestamp 80 (0x50)
   03 20             // ISR Exit. Timestamp 32 (0x20) (Shortest possible packet.)
   event tryrecv packets:
   ID|DataSize|len|String|Value|TimeStampDelta
   3D|   05   |03 |726462| 01  | B90E
   61|   5    | 3 | rdb  |  1  | 1849
   B90E = 10111001 00001110 -> 0111 00111001 =1849
*/

#include "rti.h"

static struct
{
    rt_uint32_t time_stamp_last;
    
    /* rti overflow packet count*/
    rt_uint32_t packet_count;
    
    /* rti enable status*/
    rt_uint8_t  enable;
    
    /* event disable nest*/
    rt_uint8_t  disable_nest[RTI_TRACE_NUM];
    
} rti_status;

struct rt_ringbuffer *tx_ringbuffer = RT_NULL;
static rt_thread_t tidle, rti_thread;
static const rt_uint8_t rti_sync[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static void (*rti_data_new_data_notify)(void);

/* rti recording functions */
static void rti_overflow(void);
static void rti_record_systime(void);
static void rti_on_idle(void);
static void rti_isr_enter(void);
static void rti_isr_exit(void);
static void rti_isr_to_scheduler(void);
static void rti_enter_timer(rt_uint32_t timer);
static void rti_exit_timer(rt_uint32_t timer);
static void rti_thread_start_exec(rt_uint32_t thread);
static void rti_thread_stop_exec(void);
static void rti_thread_start_ready(rt_uint32_t thread);
static void rti_thread_stop_ready(rt_uint32_t thread);
static void rti_thread_create(rt_uint32_t thread);
static void rti_record_object(rt_uint32_t rti_id, struct rt_object *object);
static void rti_send_sys_info(void);
static void rti_send_sys_desc(const char *ptr);
static void rti_send_thread_list(void);
static void rti_send_thread_info(const rt_thread_t thread);
static void rti_send_packet_void(rt_uint8_t rti_id);
static void rti_send_packet_value(rt_uint8_t rti_id, rt_uint32_t value);
static void rti_send_packet(rt_uint8_t rti_id, rt_uint8_t *packet_sta, rt_uint8_t *packet_end);

/* rti encodeing functions */
static rt_uint8_t *rti_record_ready(rt_uint8_t *start);
static rt_uint8_t *rti_encode_val(rt_uint8_t *present, rt_uint32_t value);
static rt_uint8_t *rti_encode_str(rt_uint8_t *present, const char *ptr, rt_uint8_t max_len);
static rt_uint32_t rti_shrink_id(rt_uint32_t Id);

/* rti hook functions */
static void rti_timer_enter(rt_timer_t t);
static void rti_timer_exit(rt_timer_t t);
static void rti_thread_inited(rt_thread_t thread);
static void rti_thread_suspend(rt_thread_t thread);
static void rti_thread_resume(rt_thread_t thread);
static void rti_scheduler(rt_thread_t from, rt_thread_t to);
//static void rti_object_attach(rt_object_t object);
static void rti_object_detach(rt_object_t object);
static void rti_interrupt_enter(void);
static void rti_interrupt_leave(void);
static void rti_object_trytake(rt_object_t object);
static void rti_object_take(rt_object_t object);
static void rti_object_put(rt_object_t object);

static int rti_init(void);
static rt_size_t rti_data_put(const rt_uint8_t *ptr, rt_uint16_t length);

/* rti hook functions */
static void rti_timer_enter(rt_timer_t t)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_TIMER_NUM])
        return ;
    rti_enter_timer((rt_uint32_t)t);
}

static void rti_timer_exit(rt_timer_t t)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_TIMER_NUM])
        return ;
    rti_exit_timer((rt_uint32_t)t);
}

static void rti_thread_inited(rt_thread_t thread)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_THREAD_NUM])
        return ;
    rti_thread_create((rt_uint32_t)thread);
    rti_send_thread_info(thread);
}

static void rti_thread_suspend(rt_thread_t thread)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_THREAD_NUM])
        return ;
    rti_thread_stop_ready((rt_uint32_t)thread);
}

static void rti_thread_resume(rt_thread_t thread)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_THREAD_NUM])
        return ;
    rti_thread_start_ready((rt_uint32_t)thread);
}

static void rti_scheduler(rt_thread_t from, rt_thread_t to)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_SCHEDULER_NUM])
        return ;
    rti_thread_stop_ready((rt_uint32_t)from);
    if (to == tidle)
        rti_on_idle();
    else
        rti_thread_start_exec((rt_uint32_t)to);
}

//static void rti_object_attach(rt_object_t object)
//{
//    if (!rti_status.enable || rti_status.disable_nest[RTI_THREAD_NUM])
//        return ;
//    switch (object->type & (~RT_Object_Class_Static))
//    {
//    case RT_Object_Class_Thread:
//        rti_thread_create((rt_uint32_t)object);
//        rti_send_thread_info((rt_thread_t)object);
//        break;
//    default:
//        break;
//    }
//}

static void rti_object_detach(rt_object_t object)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_THREAD_NUM])
        return ;
    switch (object->type & (~RT_Object_Class_Static))
    {
    case RT_Object_Class_Thread:
        rti_thread_stop_exec();
        break;
    default:
        break;
    }
}

static void rti_interrupt_enter(void)
{
    if (!rti_status.enable || rti_status.disable_nest[RTI_INTERRUPT_NUM])
        return ;
    rti_isr_enter();
}

static void rti_interrupt_leave(void)
{
    rt_thread_t current;

    if (!rti_status.enable || rti_status.disable_nest[RTI_INTERRUPT_NUM])
        return ;

    if (rt_interrupt_get_nest())
    {
        rti_isr_exit();
        return;
    }
    rti_isr_to_scheduler();
    current = rt_thread_self();
    if (current == tidle)
        rti_on_idle();
    else
        rti_thread_start_exec((rt_uint32_t)current);
}

static void rti_object_trytake(rt_object_t object)
{
    if (!rti_status.enable)
        return ;
    switch (object->type & (~RT_Object_Class_Static))
    {
    case RT_Object_Class_Semaphore:
        if (!rti_status.disable_nest[RTI_SEM_NUM])
            rti_record_object(RTI_ID_SEM_TRYTAKE, object);
        break;
    case RT_Object_Class_Mutex:
        if (!rti_status.disable_nest[RTI_MUTEX_NUM])
            rti_record_object(RTI_ID_MUTEX_TRYTAKE, object);
        break;
    case RT_Object_Class_Event:
        if (!rti_status.disable_nest[RTI_EVENT_NUM])
            rti_record_object(RTI_ID_EVENT_TRYTAKE, object);
        break;
    case RT_Object_Class_MailBox:
        if (!rti_status.disable_nest[RTI_MAILBOX_NUM])
            rti_record_object(RTI_ID_MAILBOX_TRYTAKE, object);
        break;
    case RT_Object_Class_MessageQueue:
        if (!rti_status.disable_nest[RTI_QUEUE_NUM])
            rti_record_object(RTI_ID_QUEUE_TRYTAKE, object);
        break;
    }
}

static void rti_object_take(rt_object_t object)
{
    if (!rti_status.enable)
        return ;
    switch (object->type & (~RT_Object_Class_Static))
    {
    case RT_Object_Class_Semaphore:
        if (!rti_status.disable_nest[RTI_SEM_NUM])
            rti_record_object(RTI_ID_SEM_TAKEN, object);
        break;
    case RT_Object_Class_Mutex:
        if (!rti_status.disable_nest[RTI_MUTEX_NUM])
            rti_record_object(RTI_ID_MUTEX_TAKEN, object);
        break;
    case RT_Object_Class_Event:
        if (!rti_status.disable_nest[RTI_EVENT_NUM])
            rti_record_object(RTI_ID_EVENT_TAKEN, object);
        break;
    case RT_Object_Class_MailBox:
        if (!rti_status.disable_nest[RTI_MAILBOX_NUM])
            rti_record_object(RTI_ID_MAILBOX_TAKEN, object);
        break;
    case RT_Object_Class_MessageQueue:
        if (!rti_status.disable_nest[RTI_QUEUE_NUM])
            rti_record_object(RTI_ID_QUEUE_TAKEN, object);
        break;
    }
}

static void rti_object_put(rt_object_t object)
{
    if (!rti_status.enable)
        return ;
    switch (object->type & (~RT_Object_Class_Static))
    {
    case RT_Object_Class_Semaphore:
        if (!rti_status.disable_nest[RTI_SEM_NUM])
            rti_record_object(RTI_ID_SEM_RELEASE, object);
        break;
    case RT_Object_Class_Mutex:
        if (!rti_status.disable_nest[RTI_MUTEX_NUM])
            rti_record_object(RTI_ID_MUTEX_RELEASE, object);
        break;
    case RT_Object_Class_Event:
        if (!rti_status.disable_nest[RTI_EVENT_NUM])
            rti_record_object(RTI_ID_EVENT_RELEASE, object);
        break;
    case RT_Object_Class_MailBox:
        if (!rti_status.disable_nest[RTI_MAILBOX_NUM])
            rti_record_object(RTI_ID_MAILBOX_RELEASE, object);
        break;
    case RT_Object_Class_MessageQueue:
        if (!rti_status.disable_nest[RTI_QUEUE_NUM])
            rti_record_object(RTI_ID_QUEUE_RELEASE, object);
        break;
    }
}

/* rti encodeing functions */
static rt_uint8_t *rti_record_ready(rt_uint8_t *start)
{
    /* reserve packet header size */
    return start + 4;
}

static rt_uint8_t *rti_encode_val(rt_uint8_t *present, rt_uint32_t value)
{
    while (value > 0x7F)
    {
        *present++ = (rt_uint8_t)(value | 0x80);
        value >>= 7;
    };
    *present++ = (rt_uint8_t)value;
    return present;
}

static rt_uint8_t *rti_encode_str(rt_uint8_t *present, const char *ptr, rt_uint8_t max_len)
{
    rt_uint8_t len = 0, n;

    while (*(ptr + len) && len < max_len)
    {
        len++;
    }
    if (len < 0xFF)
    {
        *present++ = len;
    }
    else
    {
        *present++ = 0xFF;
        *present++ = (len & 0xFF);
        *present++ = ((len >> 8) & 0xFF);
    }
    for (n = 0; n < len; n++)
    {
        *present++ = *ptr++;
    }
    return present;
}

static rt_uint32_t rti_shrink_id(rt_uint32_t Id)
{
    return ((Id) - RTI_RAM_BASE_ADDRESS) >> RTI_ID_SHIFT;
}

/* rti recording functions */
static void rti_overflow(void)
{
    rt_uint8_t packet[11];
    rt_uint8_t *present;
    rt_uint32_t time_stamp, delta;

    packet[0] = RTI_ID_OVERFLOW;
    present   = &packet[1];
    present = rti_encode_val(present, rti_status.packet_count);

    time_stamp  = RTI_GET_TIMESTAMP();
    delta = time_stamp - rti_status.time_stamp_last;
    present = rti_encode_val(present, delta);

    /* send overflow package success */
    if (rti_data_put(packet, present - packet) > 0)
    {
        rti_status.enable = RTI_ENABLE;
        rti_status.time_stamp_last = time_stamp;
        rti_status.packet_count = 0;
    }
    else
    {
        rti_status.packet_count ++;
    }
}

static void rti_record_systime(void)
{
    rt_uint8_t packet[RTI_INFO_SIZE + 2 * RTI_VALUE_SIZE];
    rt_uint8_t *start, *present;
    rt_uint64_t systime;

    systime = (rt_uint64_t)(rt_tick_get() * 1000 / RT_TICK_PER_SECOND);

    start = rti_record_ready(packet);
    present = rti_encode_val(start, (rt_uint32_t)systime);
    present = rti_encode_val(present, (rt_uint32_t)(systime >> 32));

    rti_send_packet(RTI_ID_SYSTIME_US, start, present);
}

static void rti_record_object(rt_uint32_t rti_id, struct rt_object *object)
{
    rt_uint8_t packet[RTI_INFO_SIZE + 4 * RTI_VALUE_SIZE];
    rt_uint8_t *start, *present;

    start = rti_record_ready(packet);
    present = rti_encode_str(start, object->name, RT_NAME_MAX);
    if ((object->type & (~RT_Object_Class_Static)) == RT_Object_Class_Event)
        present = rti_encode_val(present, ((rt_event_t)object)->set);

    rti_send_packet(rti_id, start, present);
}

static void rti_on_idle(void)
{
    rti_send_packet_void(RTI_ID_IDLE);
}

static void rti_isr_enter(void)
{
    rti_send_packet_value(RTI_ID_ISR_ENTER, RTI_GET_ISR_ID());
}

static void rti_isr_exit(void)
{
    rti_send_packet_void(RTI_ID_ISR_EXIT);
}

static void rti_isr_to_scheduler(void)
{
    rti_send_packet_void(RTI_ID_ISR_TO_SCHEDULER);
}

static void rti_enter_timer(rt_uint32_t timer)
{
    rti_send_packet_value(RTI_ID_TIMER_ENTER, rti_shrink_id(timer));
}

static void rti_exit_timer(rt_uint32_t timer)
{
    rti_send_packet_void(RTI_ID_TIMER_EXIT);
}

static void rti_thread_start_exec(rt_uint32_t thread)
{
    rti_send_packet_value(RTI_ID_THREAD_START_EXEC, rti_shrink_id(thread));
}

static void rti_thread_stop_exec(void)
{
    rti_send_packet_void(RTI_ID_THREAD_STOP_EXEC);
}

static void rti_thread_start_ready(rt_uint32_t thread)
{
    rti_send_packet_value(RTI_ID_THREAD_START_READY, rti_shrink_id(thread));
}

static void rti_thread_stop_ready(rt_uint32_t thread)
{
    rt_uint8_t packet[RTI_INFO_SIZE + 2 * RTI_VALUE_SIZE];
    rt_uint8_t *start, *present;

    start = rti_record_ready(packet);
    present = rti_encode_val(start, rti_shrink_id(thread));
    present = rti_encode_val(present, 0);

    rti_send_packet(RTI_ID_THREAD_STOP_READY, start, present);
}

static void rti_thread_create(rt_uint32_t thread)
{
    rti_send_packet_value(RTI_ID_THREAD_CREATE, rti_shrink_id(thread));
}

static void rti_send_sys_desc(const char *ptr)
{
    rt_uint8_t packet[RTI_INFO_SIZE + 1 + RTI_MAX_STRING_LEN];
    rt_uint8_t *start, *present;

    start = rti_record_ready(packet);
    present = rti_encode_str(start, ptr, RTI_MAX_STRING_LEN);

    rti_send_packet(RTI_ID_SYSDESC, start, present);
}

static void rti_send_sys_info(void)
{
    // Add sync packet ( 10 * 0x00)
    // Send system description
    // Send system time
    // Send thread list
    rti_data_put(rti_sync, 10);
    rti_send_packet_void(RTI_ID_START);
    {
        rt_uint8_t packet[RTI_INFO_SIZE + 4 * RTI_VALUE_SIZE];
        rt_uint8_t *start, *present;

        start = rti_record_ready(packet);
        present = rti_encode_val(start, RTI_SYS_FREQ);
        present = rti_encode_val(present, RTI_CPU_FREQ);
        present = rti_encode_val(present, RTI_RAM_BASE_ADDRESS);
        present = rti_encode_val(present, RTI_ID_SHIFT);

        rti_send_packet(RTI_ID_INIT, start, present);
    }
    rti_send_sys_desc("N="RTI_APP_NAME",O=RT-Thread");
    rti_send_sys_desc(RTI_SYS_DESC0);
    rti_send_sys_desc(RTI_SYS_DESC1);
    rti_record_systime();
    rti_send_thread_list();
}

static void rti_send_thread_info(const rt_thread_t thread)
{
    rt_uint8_t packet[RTI_INFO_SIZE + RTI_VALUE_SIZE + 1 + 32];
    rt_uint8_t *start, *present;

    rt_enter_critical();
    start = rti_record_ready(packet);
    present = rti_encode_val(start, rti_shrink_id((rt_uint32_t)thread));
    present = rti_encode_val(present, thread->current_priority);
    present = rti_encode_str(present, thread->name, 32);
    rti_send_packet(RTI_ID_THREAD_INFO, start, present);

    present = rti_encode_val(start, rti_shrink_id((rt_uint32_t)thread));
    present = rti_encode_val(present, (rt_uint32_t)thread->stack_addr);
    present = rti_encode_val(present, thread->stack_size);
    present = rti_encode_val(present, 0);
    rti_send_packet(RTI_ID_STACK_INFO, start, present);
    rt_exit_critical();
}

static void rti_send_thread_list(void)
{
    struct rt_thread *thread;
    struct rt_list_node *node;
    struct rt_list_node *list;
    struct rt_object_information *info;

    info = rt_object_get_information(RT_Object_Class_Thread);
    list = &info->object_list;

    tidle = rt_thread_idle_gethandler();

    rt_enter_critical();
    for (node = list->next; node != list; node = node->next)
    {
        thread = rt_list_entry(node, struct rt_thread, list);
        /* skip idle thread */
        if (thread != tidle)
            rti_send_thread_info(thread);
    }
    rt_exit_critical();
}

/* send a void package */
static void rti_send_packet_void(rt_uint8_t rti_id)
{
    rt_uint8_t packet[RTI_INFO_SIZE];
    rt_uint8_t *start;

    start = rti_record_ready(packet);
    rti_send_packet(rti_id, start, start);
}

/* send a value package */
static void rti_send_packet_value(rt_uint8_t rti_id, rt_uint32_t value)
{
    rt_uint8_t packet[RTI_INFO_SIZE + RTI_VALUE_SIZE];
    rt_uint8_t *start, *present;

    present = start = rti_record_ready(packet);
    present = rti_encode_val(present, value);

    rti_send_packet(rti_id, start, present);
}

void rti_print(const char *s)
{
    rt_uint8_t packet[RTI_INFO_SIZE + 2 * RTI_VALUE_SIZE + RTI_MAX_STRING_LEN];
    rt_uint8_t *start, *present;

    start = rti_record_ready(packet);
    present = rti_encode_str(start, s, RTI_MAX_STRING_LEN);
    present = rti_encode_val(present, RTI_LOG);
    present = rti_encode_val(present, 0);

    rti_send_packet(RTI_ID_PRINT_FORMATTED, start, present);
}

static void rti_send_packet(rt_uint8_t rti_id, rt_uint8_t *packet_sta, rt_uint8_t *packet_end)
{
    rt_uint16_t  len;
    rt_uint32_t  time_stamp, delta;

    if (rti_status.enable == RTI_DISABLE)
        return ;
    if (rti_id < 24)
    {
        *--packet_sta = rti_id;
    }
    else
    {
        len = packet_end - packet_sta;
        if (len > 127)
        {
            *--packet_sta = (len >> 7);
            *--packet_sta = len | 0x80;
        }
        else
        {
            *--packet_sta = len;
        }
        if (rti_id > 127)
        {
            *--packet_sta = (rti_id >> 7);
            *--packet_sta = rti_id | 0x80;
        }
        else
        {
            *--packet_sta = rti_id;
        }
    }
    time_stamp  = RTI_GET_TIMESTAMP();
    delta = time_stamp - rti_status.time_stamp_last;
    packet_end = rti_encode_val(packet_end, delta);
    if (rti_status.enable == RTI_OVERFLOW)
    {
        rti_overflow();
        if (rti_status.enable != RTI_ENABLE)
            return ;
    }
    /* overflow */
    if (rti_data_put(packet_sta, packet_end - packet_sta) == 0)
    {
        rti_status.enable = RTI_OVERFLOW;
        rti_status.packet_count ++;
        rti_overflow();
    }

    rti_status.time_stamp_last = time_stamp;
}

/*
 * rti api function.
 */
void rti_data_new_data_notify_set_hook(void (*hook)(void))
{
    rti_data_new_data_notify = hook;
}
void rti_trace_disable(rt_uint16_t flag)
{
    register rt_ubase_t temp;
    temp = rt_hw_interrupt_disable();

    for (rt_uint8_t i = 0; i < RTI_TRACE_NUM; i++)
    {
        if (flag & (1 << i))
        {
            rti_status.disable_nest[i] ++;
        }
    }
    rt_hw_interrupt_enable(temp);
}

void rti_trace_enable(rt_uint16_t flag)
{
    register rt_ubase_t temp;
    temp = rt_hw_interrupt_disable();

    for (rt_uint8_t i = 0; i < RTI_TRACE_NUM; i++)
    {
        if (flag & (1 << i))
        {
            rti_status.disable_nest[i] --;
        }
    }
    rt_hw_interrupt_enable(temp);
}

static rt_size_t rti_data_put(const rt_uint8_t *ptr, rt_uint16_t length)
{
    register rt_ubase_t temp;
    rt_size_t size = 0;
    if (!rti_status.enable)
        return 0;

    temp = rt_hw_interrupt_disable();
    if (rt_ringbuffer_space_len(tx_ringbuffer) > length)
        size = rt_ringbuffer_put(tx_ringbuffer, ptr, length);
    if (rt_ringbuffer_data_len(tx_ringbuffer) > RTI_BUFFER_SIZE / 2)
    {
        if (rti_thread != RT_NULL)
        {
            rti_trace_disable(RTI_ALL);
            rt_thread_resume(rti_thread);
            rti_trace_enable(RTI_ALL);
            rti_thread = RT_NULL;
        }
    }
    rt_hw_interrupt_enable(temp);
    return size;
}

rt_size_t rti_data_get(rt_uint8_t *ptr, rt_uint16_t length)
{
    if (tx_ringbuffer == RT_NULL)
        return 0;
    return rt_ringbuffer_get(tx_ringbuffer, ptr, length);;
}

rt_size_t rti_buffer_used(void)
{
    return rt_ringbuffer_data_len(tx_ringbuffer);
}

void rti_start(void)
{
    rt_kprintf("rti start\n");
    rt_ringbuffer_reset(tx_ringbuffer);
    rti_status.enable = RTI_ENABLE;
    rti_send_sys_info();
}

void rti_stop(void)
{
    rti_status.enable = RTI_DISABLE;
    rt_kprintf("rti stop\n");
    if (rti_thread != RT_NULL)
    {
        rt_thread_resume(rti_thread);
        rti_thread = RT_NULL;
    }
}

static void rti_thread_entry(void *parameter)
{
    register rt_ubase_t temp;
    rt_thread_t thread;

    while (1)
    {
        thread = rt_thread_self();
        while (rti_status.enable && (rti_buffer_used() > RTI_DATE_PACKAGE_SIZE))
        {
            RT_OBJECT_HOOK_CALL(rti_data_new_data_notify, ());
        }
        temp = rt_hw_interrupt_disable();

        rti_thread = thread;
        rt_thread_suspend(thread);

        rt_hw_interrupt_enable(temp);

        rt_schedule();
    }
}

static int rti_init(void)
{
    rt_thread_t rti_thread = RT_NULL;

    tidle = rt_thread_idle_gethandler();

    tx_ringbuffer = rt_ringbuffer_create(RTI_BUFFER_SIZE);
    if (tx_ringbuffer == RT_NULL)
        return -1;

    rti_thread = rt_thread_create("rti",
                                  rti_thread_entry,
                                  RT_NULL,
                                  1024, 20, 5);
    if (rti_thread != RT_NULL)
        rt_thread_startup(rti_thread);
    else
    {
        rt_ringbuffer_destroy(tx_ringbuffer);
        return -1;
    }
    /* register hooks */
    //rt_object_attach_sethook(rti_object_attach);
    rt_object_detach_sethook(rti_object_detach);
    rt_object_trytake_sethook(rti_object_trytake);
    rt_object_take_sethook(rti_object_take);
    rt_object_put_sethook(rti_object_put);

    rt_thread_suspend_sethook(rti_thread_suspend);
    rt_thread_resume_sethook(rti_thread_resume);
    rt_thread_inited_sethook(rti_thread_inited);
    rt_scheduler_sethook(rti_scheduler);

    rt_timer_enter_sethook(rti_timer_enter);
    rt_timer_exit_sethook(rti_timer_exit);

    rt_interrupt_enter_sethook(rti_interrupt_enter);
    rt_interrupt_leave_sethook(rti_interrupt_leave);

    return 0;
}
INIT_COMPONENT_EXPORT(rti_init);
