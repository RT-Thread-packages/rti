/*
 * File      : rti_config.h
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
#ifndef __RTI_CONFIG_H__
#define __RTI_CONFIG_H__

#include "rtconfig.h"
#include "rtdevice.h"

/* RTI interrupt configuration */
#if defined(ARCH_ARM_CORTEX_M3) || defined(ARCH_ARM_CORTEX_M4) || defined(ARCH_ARM_CORTEX_M7)
    #define RTI_GET_ISR_ID()   ((*(rt_uint32_t *)(0xE000ED04)) & 0x1FF)    // Get the currently active interrupt Id. (i.e. read Cortex-M ICSR[8:0] = active vector)
#elif defined(ARCH_ARM_CORTEX_M0)
    #if defined(__ICCARM__)
        #define RTI_GET_ISR_ID()   (__get_IPSR())                          // Workaround for IAR, which might do a byte-access to 0xE000ED04. Read IPSR instead.
    #else
        #define RTI_GET_ISR_ID()   ((*(rt_uint32_t *)(0xE000ED04)) & 0x3F) // Get the currently active interrupt Id. (i.e. read Cortex-M ICSR[5:0] = active vector)
    #endif
#elif defined(ARCH_ARM_GIC)
    #undef RTI_GET_ISR_ID()
    void rti_interrupt_gic_enter(int irqno);                               // Call in GIC interrupt handler function
#else
    #error "This kernel is not currently supported, You can implement this function yourself"
    #define RTI_GET_ISR_ID()                                               // Get the currently active interrupt Id from the user-provided function.
#endif

/* RTI buffer configuration */
#ifndef PKG_USING_RTI
    #define RTI_BUFFER_SIZE        2048                  // Number of bytes that RTI uses for the buffer.
#else
    #define RTI_BUFFER_SIZE        PKG_RTI_BUFFER_SIZE
#endif

/* RTI Id configuration */
#ifndef PKG_USING_RTI
    #define RTI_RAM_BASE_ADDRESS         0x20000000      // Default value for the lowest Id reported by the application.
    #define RTI_ID_SHIFT                 2               // Number of bits to shift the Id to save bandwidth. (i.e. 2 when Ids are 4 byte aligned)
#else
    #define RTI_RAM_BASE_ADDRESS         PKG_RTI_RAM_BASE
    #define RTI_ID_SHIFT                 PKG_RTI_ID_SHIFT
#endif

/* The application name to be displayed in RTI */
#ifndef   RTI_APP_NAME
    #ifndef PKG_USING_RTI
        #define RTI_APP_NAME        "RT-Thread RTI"
    #else
        #define RTI_APP_NAME        PKG_RTI_APP_NAME
    #endif
#endif

#ifndef   RTI_SYS_DESC0
    #ifndef PKG_USING_RTI
        #define RTI_SYS_DESC0        "I#15=SysTick"
    #else
        #define RTI_SYS_DESC0        PKG_RTI_SYS_DESC0
    #endif
#endif

#ifndef   RTI_SYS_DESC1
    #ifndef PKG_USING_RTI
        #define RTI_SYS_DESC1        ""
    #else
        #define RTI_SYS_DESC1        PKG_RTI_SYS_DESC1
    #endif
#endif

extern unsigned int             SystemCoreClock;
#define rt_uint64_t             unsigned long long

#define RTI_GET_TIMESTAMP()     clock_cpu_gettime()
#define RTI_SYS_FREQ            (SystemCoreClock)
#define RTI_CPU_FREQ            (SystemCoreClock)
#define RTI_MAX_STRING_LEN      128
#define RTI_DATE_PACKAGE_SIZE   1024

#endif
