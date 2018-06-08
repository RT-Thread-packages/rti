/*
 * File      : rti.h
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
#ifndef __RTI_H__
#define __RTI_H__

#include <rtthread.h>
#include <rthw.h>
#include "rti_config.h"

/* rti events. First 32 IDs from 0 .. 31 are Compatible for Systemvie */
#define   RTI_ID_NOP                (0u)
#define   RTI_ID_OVERFLOW           (1u)
#define   RTI_ID_ISR_ENTER          (2u)
#define   RTI_ID_ISR_EXIT           (3u)
#define   RTI_ID_THREAD_START_EXEC  (4u)
#define   RTI_ID_THREAD_STOP_EXEC   (5u)
#define   RTI_ID_THREAD_START_READY (6u)
#define   RTI_ID_THREAD_STOP_READY  (7u)
#define   RTI_ID_THREAD_CREATE      (8u)
#define   RTI_ID_THREAD_INFO        (9u)
#define   RTI_ID_START             (10u)
#define   RTI_ID_STOP              (11u)
#define   RTI_ID_SYSTIME_CYCLES    (12u)
#define   RTI_ID_SYSTIME_US        (13u)
#define   RTI_ID_SYSDESC           (14u)
#define   RTI_ID_USER_START        (15u)
#define   RTI_ID_USER_STOP         (16u)
#define   RTI_ID_IDLE              (17u)
#define   RTI_ID_ISR_TO_SCHEDULER  (18u)
#define   RTI_ID_TIMER_ENTER       (19u)
#define   RTI_ID_TIMER_EXIT        (20u)
#define   RTI_ID_STACK_INFO        (21u)
#define   RTI_ID_MODULEDESC        (22u)

#define   RTI_ID_INIT              (24u)
#define   RTI_ID_NAME_RESOURCE     (25u)
#define   RTI_ID_PRINT_FORMATTED   (26u)
#define   RTI_ID_NUMMODULES        (27u)
#define   RTI_ID_END_CALL          (28u)
#define   RTI_ID_THREAD_TERMINATE  (29u)

#define   RTI_ID_EX                (31u)

#define   RTI_ID_SEM_BASE         (40u)
#define   RTI_ID_SEM_TRYTAKE      ( 1u + RTI_ID_SEM_BASE)
#define   RTI_ID_SEM_TAKEN        ( 2u + RTI_ID_SEM_BASE)
#define   RTI_ID_SEM_RELEASE      ( 3u + RTI_ID_SEM_BASE)

#define   RTI_ID_MUTEX_BASE       (50u)
#define   RTI_ID_MUTEX_TRYTAKE    ( 1u + RTI_ID_MUTEX_BASE)
#define   RTI_ID_MUTEX_TAKEN      ( 2u + RTI_ID_MUTEX_BASE)
#define   RTI_ID_MUTEX_RELEASE    ( 3u + RTI_ID_MUTEX_BASE)

#define   RTI_ID_EVENT_BASE       (60u)
#define   RTI_ID_EVENT_TRYTAKE    ( 1u + RTI_ID_EVENT_BASE)
#define   RTI_ID_EVENT_TAKEN      ( 2u + RTI_ID_EVENT_BASE)
#define   RTI_ID_EVENT_RELEASE    ( 3u + RTI_ID_EVENT_BASE)

#define   RTI_ID_MAILBOX_BASE     (70u)
#define   RTI_ID_MAILBOX_TRYTAKE  ( 1u + RTI_ID_MAILBOX_BASE)
#define   RTI_ID_MAILBOX_TAKEN    ( 2u + RTI_ID_MAILBOX_BASE)
#define   RTI_ID_MAILBOX_RELEASE  ( 3u + RTI_ID_MAILBOX_BASE)

#define   RTI_ID_QUEUE_BASE       (80u)
#define   RTI_ID_QUEUE_TRYTAKE    ( 1u + RTI_ID_QUEUE_BASE)
#define   RTI_ID_QUEUE_TAKEN      ( 2u + RTI_ID_QUEUE_BASE)
#define   RTI_ID_QUEUE_RELEASE    ( 3u + RTI_ID_QUEUE_BASE)

/*trace event flag*/
#define RTI_SEM_NUM        (0)
#define RTI_MUTEX_NUM      (1)
#define RTI_EVENT_NUM      (2)
#define RTI_MAILBOX_NUM    (3)
#define RTI_QUEUE_NUM      (4)
#define RTI_THREAD_NUM     (5)
#define RTI_SCHEDULER_NUM  (6)
#define RTI_INTERRUPT_NUM  (7)
#define RTI_TIMER_NUM      (8)
#define RTI_TRACE_NUM      (9)

#define RTI_SEM            (1 << RTI_SEM_NUM      )
#define RTI_MUTEX          (1 << RTI_MUTEX_NUM    )
#define RTI_EVENT          (1 << RTI_EVENT_NUM    )
#define RTI_MAILBOX        (1 << RTI_MAILBOX_NUM  )
#define RTI_QUEUE          (1 << RTI_QUEUE_NUM    )
#define RTI_THREAD         (1 << RTI_THREAD_NUM   )
#define RTI_SCHEDULER      (1 << RTI_SCHEDULER_NUM)
#define RTI_INTERRUPT      (1 << RTI_INTERRUPT_NUM)
#define RTI_TIMER          (1 << RTI_TIMER_NUM    )
#define RTI_ALL            (0x01FF)

/* rti data size */
#define RTI_INFO_SIZE      (9)
#define RTI_VALUE_SIZE     (5)

/* rti enable status */
#define RTI_DISABLE         0
#define RTI_ENABLE          1
#define RTI_OVERFLOW        2

/* rti api */
void rti_start(void);
void rti_stop(void);
void rti_trace_enable(rt_uint16_t flag);
void rti_trace_disable(rt_uint16_t flag);
rt_size_t rti_data_get(rt_uint8_t *ptr, rt_uint16_t length);
rt_size_t rti_buffer_used(void);
void rti_data_new_data_notify_set_hook(void (*hook)(void));

#endif
