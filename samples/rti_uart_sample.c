/*
 * File      : rti_uart_sample.c
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

#include <rtthread.h>
#include <rtdevice.h>

#include "rti.h"

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#define BUF_SIZE                 (512)
#define UART_CHANGE              "uart3"
#define UART_BAUD_RATE           BAUD_RATE_921600

static rt_uint8_t buf[BUF_SIZE];
static rt_device_t rti_dev;

void rti_data_new_data_notify(void)
{
    int ret = rti_data_get(buf, BUF_SIZE);
    rt_device_write(rti_dev, 0, buf, ret);
}

rt_err_t rt_ind(rt_device_t dev, rt_size_t size)
{
    rti_data_new_data_notify_set_hook(rti_data_new_data_notify);
    rti_start();
    return 0;
}

void rti_uart_sample(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    rti_dev = rt_console_get_device();

#ifdef RT_USING_FINSH
    finsh_set_device(UART_CHANGE);
#endif
    rt_console_set_device(UART_CHANGE);

    rti_dev->open_flag &= ~RT_DEVICE_FLAG_STREAM;

    config.baud_rate = UART_BAUD_RATE;
    rt_device_control(rti_dev, RT_DEVICE_CTRL_CONFIG, &config);

    rt_device_set_rx_indicate(rti_dev, rt_ind);
    rt_device_open(rti_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
}
#ifdef RT_USING_FINSH
MSH_CMD_EXPORT(rti_uart_sample, start record by uart.);
#endif
