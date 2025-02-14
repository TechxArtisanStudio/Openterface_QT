// SPDX-License-Identifier: GPL-2.0+
/*
 * CH340/CH341 USB to serial port driver
 *
 * Copyright (C) 2024 Nanjing Qinheng Microelectronics Co., Ltd.
 * Web:      http://wch.cn
 * Author:   WCH <tech@wch.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * System require:
 * Kernel version beyond 3.2.x
 *
 * Update Log:
 * V1.0 - initial version
 * V1.1 - add support high baudrates, modem signal, flow control, etc.
 * V1.2 - fixe baud rate 0 bugs and add some debug messages
 * V1.3 - special setting on usb packet upload timeout
 * V1.4 - change read urb length to ep size
 * V1.5 - fix hardware flowcontrol bugs
 * V1.6 - add support for application to get uart state
 *      - remove tty throttle methods
 *      - submit urbs when uart open while not probe
 *      - fix tty kref errors in dtr_rts
 *      - add support for kernel version beyond 5.14.x
 *      - fix data analysis in status ep callback
 * V1.7 - add support for kernel version beyond 6.3.x
 * V1.8 - add support for kernel version beyond 6.5.x
 */

#define DEBUG
#define VERBOSE_DEBUG

#undef DEBUG
#undef VERBOSE_DEBUG

#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/version.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
#include <linux/sched/signal.h>
#endif

#include "ch341.h"

#define DRIVER_AUTHOR "WCH"
#define DRIVER_DESC "USB serial driver for ch340, ch341, etc."
#define VERSION_DESC "V1.8 On 2024.08"

static struct usb_driver ch341_driver;
static struct tty_driver *ch341_tty_driver;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
static DEFINE_IDR(ch341_minors);
#else
static struct ch341 *ch341_table[CH341_TTY_MINORS];
#endif
static DEFINE_MUTEX(ch341_minors_lock);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
static void ch341_tty_set_termios(struct tty_struct *tty,
				  const struct ktermios *termios_old);
#else
static void ch341_tty_set_termios(struct tty_struct *tty,
				  struct ktermios *termios_old);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
/*
 * Look up an ch341 structure by minor. If found and not disconnected,
 * increment its refcount and return it with its mutex held.
 */
static struct ch341 *ch341_get_by_index(unsigned int minor)
{
	struct ch341 *ch341;

	mutex_lock(&ch341_minors_lock);
	ch341 = idr_find(&ch341_minors, minor);
	if (ch341) {
		mutex_lock(&ch341->mutex);
		if (ch341->disconnected) {
			mutex_unlock(&ch341->mutex);
			ch341 = NULL;
		} else {
			tty_port_get(&ch341->port);
			mutex_unlock(&ch341->mutex);
		}
	}
	mutex_unlock(&ch341_minors_lock);
	return ch341;
}

/*
 * Try to find an available minor number and if found,
 * associate it with 'ch341'.
 */
static int ch341_alloc_minor(struct ch341 *ch341)
{
	int minor;

	mutex_lock(&ch341_minors_lock);
	minor = idr_alloc(&ch341_minors, ch341, 0, CH341_TTY_MINORS,
			  GFP_KERNEL);
	mutex_unlock(&ch341_minors_lock);

	return minor;
}

/* Release the minor number associated with 'ch341'. */
static void ch341_release_minor(struct ch341 *ch341)
{
	mutex_lock(&ch341_minors_lock);
	idr_remove(&ch341_minors, ch341->minor);
	mutex_unlock(&ch341_minors_lock);
}

#else

/*
 * Look up an CH341 structure by index. If found and not disconnected,
 * increment its refcount and return it with its mutex held.
 */
static struct ch341 *ch341_get_by_index(unsigned int index)
{
	struct ch341 *ch341;

	mutex_lock(&ch341_minors_lock);
	ch341 = ch341_table[index];
	if (ch341) {
		mutex_lock(&ch341->mutex);
		if (ch341->disconnected) {
			mutex_unlock(&ch341->mutex);
			ch341 = NULL;
		} else {
			tty_port_get(&ch341->port);
			mutex_unlock(&ch341->mutex);
		}
	}
	mutex_unlock(&ch341_minors_lock);

	return ch341;
}

/*
 * Try to find an available minor number and if found,
 * associate it with 'ch341'.
 */
static int ch341_alloc_minor(struct ch341 *ch341)
{
	int minor;

	mutex_lock(&ch341_minors_lock);
	for (minor = 0; minor < CH341_TTY_MINORS; minor++) {
		if (!ch341_table[minor]) {
			ch341_table[minor] = ch341;
			break;
		}
	}
	mutex_unlock(&ch341_minors_lock);

	return minor;
}

/* Release the minor number associated with 'ch341'. */
static void ch341_release_minor(struct ch341 *ch341)
{
	mutex_lock(&ch341_minors_lock);
	ch341_table[ch341->minor] = NULL;
	mutex_unlock(&ch341_minors_lock);
}

#endif
static int ch341_control_out(struct ch341 *ch341, u8 request, u16 value,
			     u16 index)
{
	int retval;

	retval = usb_autopm_get_interface(ch341->data);
	if (retval)
		return retval;

	retval = usb_control_msg(
		ch341->dev, usb_sndctrlpipe(ch341->dev, 0), request,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT, value,
		index, NULL, 0, DEFAULT_TIMEOUT);

	usb_autopm_put_interface(ch341->data);

	return retval < 0 ? retval : 0;
}

static int ch341_control_in(struct ch341 *ch341, u8 request, u16 value,
			    u16 index, char *buf, unsigned bufsize)
{
	int retval;

	retval = usb_autopm_get_interface(ch341->data);
	if (retval)
		return retval;

	retval = usb_control_msg(
		ch341->dev, usb_rcvctrlpipe(ch341->dev, 0), request,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN, value,
		index, buf, bufsize, DEFAULT_TIMEOUT);

	usb_autopm_put_interface(ch341->data);

	return retval;
}

static inline int ch341_set_control(struct ch341 *ch341, int control)
{
	u16 value = 0;

	value |= (u8)~control;

	return ch341_control_out(ch341, CMD_C2, value, 0x0000);
}

static inline int ch341_set_line(struct ch341 *ch341,
				 struct usb_ch341_line_coding *line)
{
	return 0;
}

static int ch341_get_status(struct ch341 *ch341)
{
	char *buffer;
	int retval;
	const unsigned size = 2;
	unsigned long flags;

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	retval = ch341_control_in(ch341, CMD_R, 0x0706, 0, buffer, size);
	if (retval < 0)
		goto out;

	if (retval > 0) {
		spin_lock_irqsave(&ch341->read_lock, flags);
		ch341->ctrlin = (~(*buffer)) & CH341_CTI_ST;
		spin_unlock_irqrestore(&ch341->read_lock, flags);
	} else
		retval = -EPROTO;

out:
	kfree(buffer);
	return retval;
}

static int ch341_configure(struct ch341 *ch341)
{
	char *buffer;
	int r;
	const unsigned size = 2;

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	r = ch341_control_in(ch341, CMD_C3, 0, 0, buffer, size);
	if (r < 0)
		goto out;
	r = ch341_control_out(ch341, CMD_C1, 0, 0);
	if (r < 0)
		goto out;
	r = ch341_control_out(ch341, CMD_W, 0x1312, 0xd982);
	if (r < 0)
		goto out;
	r = ch341_control_out(ch341, CMD_W, 0x0f2c, 0x0007);
	if (r < 0)
		goto out;
	r = ch341_control_in(ch341, CMD_R, 0x2518, 0, buffer, size);
	if (r < 0)
		goto out;
	r = ch341_get_status(ch341);
	if (r < 0)
		goto out;
	r = ch341_control_out(ch341, CMD_W, 0x2727, 0);
	if (r < 0)
		goto out;

out:
	kfree(buffer);
	return r;
}

/*
 * Write buffer management.
 * All of these assume proper locks taken by the caller.
 */
static int ch341_wb_alloc(struct ch341 *ch341)
{
	int i, wbn;
	struct ch341_wb *wb;

	wbn = 0;
	i = 0;
	for (;;) {
		wb = &ch341->wb[wbn];
		if (!wb->use) {
			wb->use = 1;
			return wbn;
		}
		wbn = (wbn + 1) % CH341_NW;
		if (++i >= CH341_NW)
			return -1;
	}
}

static int ch341_wb_is_avail(struct ch341 *ch341)
{
	int i, n;
	unsigned long flags;

	n = CH341_NW;
	spin_lock_irqsave(&ch341->write_lock, flags);
	for (i = 0; i < CH341_NW; i++)
		n -= ch341->wb[i].use;
	spin_unlock_irqrestore(&ch341->write_lock, flags);
	return n;
}

static void ch341_write_done(struct ch341 *ch341, struct ch341_wb *wb)
{
	wb->use = 0;
	ch341->transmitting--;
	usb_autopm_put_interface_async(ch341->data);
}

static int ch341_start_wb(struct ch341 *ch341, struct ch341_wb *wb)
{
	int rc;

	ch341->transmitting++;

	wb->urb->transfer_buffer = wb->buf;
	wb->urb->transfer_dma = wb->dmah;
	wb->urb->transfer_buffer_length = wb->len;
	wb->urb->dev = ch341->dev;

	rc = usb_submit_urb(wb->urb, GFP_ATOMIC);
	if (rc < 0) {
		dev_err(&ch341->data->dev,
			"%s - usb_submit_urb(write bulk) failed: %d\n",
			__func__, rc);
		ch341_write_done(ch341, wb);
	}
	return rc;
}

static void ch341_update_status(struct ch341 *ch341, unsigned char *data,
				size_t len)
{
	unsigned long flags;
	u8 status;
	u8 difference;
	u8 type = data[0];
	u8 handled = 0;

	if (len < 4)
		return;

	if (type & CH341_CTT_M) {
		status = ~data[2] & CH341_CTI_ST;

		if (!ch341->clocal &&
		    (ch341->ctrlin & status & CH341_CTI_DC)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
			struct tty_struct *tty =
				tty_port_tty_get(&ch341->port);
			tty_hangup(tty);
#else
			tty_port_tty_hangup(&ch341->port, false);
#endif
		}

		spin_lock_irqsave(&ch341->read_lock, flags);
		difference = status ^ ch341->ctrlin;
		ch341->ctrlin = status;
		ch341->oldcount = ch341->iocount;
		if (difference) {
			if (difference & CH341_CTI_C) {
				ch341->iocount.cts++;
			}
			if (difference & CH341_CTI_DS) {
				ch341->iocount.dsr++;
			}
			if (difference & CH341_CTRL_RI) {
				ch341->iocount.rng++;
			}
			if (difference & CH341_CTI_DC) {
				ch341->iocount.dcd++;
			}
			spin_unlock_irqrestore(&ch341->read_lock, flags);
			wake_up_interruptible(&ch341->wioctl);
		} else
			spin_unlock_irqrestore(&ch341->read_lock, flags);
		handled = 1;
	}
	if (type & CH341_CTT_O) {
		spin_lock_irqsave(&ch341->read_lock, flags);
		ch341->oldcount = ch341->iocount;
		ch341->iocount.overrun++;
		spin_unlock_irqrestore(&ch341->read_lock, flags);
		handled = 1;
	}
	if ((type & CH341_CTT_F) == CH341_CTT_F) {
		spin_lock_irqsave(&ch341->read_lock, flags);
		ch341->oldcount = ch341->iocount;
		ch341->iocount.frame++;
		spin_unlock_irqrestore(&ch341->read_lock, flags);
		handled = 1;
	} else if (type & CH341_CTT_P) {
		spin_lock_irqsave(&ch341->read_lock, flags);
		ch341->oldcount = ch341->iocount;
		ch341->iocount.parity++;
		spin_unlock_irqrestore(&ch341->read_lock, flags);
		handled = 1;
	}
	if (!handled)
		dev_dbg(&ch341->data->dev,
			"%s - unknown status received:"
			"len:%d, data0:0x%x, data1:0x%x\n",
			__func__, (int)len, data[0], data[1]);
}

static void ch341_ctrl_irq(struct urb *urb)
{
	struct ch341 *ch341 = urb->context;
	unsigned char *data = urb->transfer_buffer;
	unsigned int len = urb->actual_length;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&ch341->data->dev,
			"%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&ch341->data->dev,
			"%s - nonzero urb status received: %d\n", __func__,
			status);
		goto exit;
	}

	usb_mark_last_busy(ch341->dev);
	ch341_update_status(ch341, data, len);
exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval && retval != -EPERM)
		dev_err(&ch341->data->dev,
			"%s - usb_submit_urb failed: %d\n", __func__,
			retval);
}

static int ch341_submit_read_urb(struct ch341 *ch341, int index,
				 gfp_t mem_flags)
{
	int res;

	if (!test_and_clear_bit(index, &ch341->read_urbs_free))
		return 0;

	res = usb_submit_urb(ch341->read_urbs[index], mem_flags);
	if (res) {
		if (res != -EPERM) {
			dev_err(&ch341->data->dev,
				"%s - usb_submit_urb failed: %d\n",
				__func__, res);
		}
		set_bit(index, &ch341->read_urbs_free);
		return res;
	}
	return 0;
}

static int ch341_submit_read_urbs(struct ch341 *ch341, gfp_t mem_flags)
{
	int res;
	int i;

	for (i = 0; i < ch341->rx_buflimit; ++i) {
		res = ch341_submit_read_urb(ch341, i, mem_flags);
		if (res)
			return res;
	}
	return 0;
}

static void ch341_process_read_urb(struct ch341 *ch341, struct urb *urb)
{
	if (!urb->actual_length)
		return;

	ch341->iocount.rx += urb->actual_length;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	tty_insert_flip_string(&ch341->port, urb->transfer_buffer,
			       urb->actual_length);
	tty_flip_buffer_push(&ch341->port);
#else
	struct tty_struct *tty = tty_port_tty_get(&ch341->port);
	tty_insert_flip_string(tty, urb->transfer_buffer,
			       urb->actual_length);
	tty_flip_buffer_push(tty);
#endif
}

static void ch341_read_bulk_callback(struct urb *urb)
{
	struct ch341_rb *rb = urb->context;
	struct ch341 *ch341 = rb->instance;
	int status = urb->status;

	if (!ch341->dev) {
		set_bit(rb->index, &ch341->read_urbs_free);
		dev_dbg(&ch341->data->dev, "%s - disconnected\n",
			__func__);
		return;
	}
	if (status) {
		set_bit(rb->index, &ch341->read_urbs_free);
		dev_dbg(&ch341->data->dev,
			"%s - non-zero urb status: %d\n", __func__,
			status);
		return;
	}
	usb_mark_last_busy(ch341->dev);
	ch341_process_read_urb(ch341, urb);
	set_bit(rb->index, &ch341->read_urbs_free);
	ch341_submit_read_urb(ch341, rb->index, GFP_ATOMIC);
}

/* data interface wrote those outgoing bytes */
static void ch341_write_bulk(struct urb *urb)
{
	struct ch341_wb *wb = urb->context;
	struct ch341 *ch341 = wb->instance;
	unsigned long flags;
	int status = urb->status;

	if (status || (urb->actual_length != urb->transfer_buffer_length))
		dev_vdbg(&ch341->data->dev, "%s - len %d/%d, status %d\n",
			 __func__, urb->actual_length,
			 urb->transfer_buffer_length, status);

	ch341->iocount.tx += urb->actual_length;
	spin_lock_irqsave(&ch341->write_lock, flags);
	ch341_write_done(ch341, wb);
	spin_unlock_irqrestore(&ch341->write_lock, flags);
	schedule_work(&ch341->work);
}

static void ch341_softint(struct work_struct *work)
{
	struct ch341 *ch341 = container_of(work, struct ch341, work);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
	struct tty_struct *tty;

	tty = tty_port_tty_get(&ch341->port);
	if (!tty)
		return;
	tty_wakeup(tty);
	tty_kref_put(tty);
#else
	tty_port_tty_wakeup(&ch341->port);
#endif
}

static int ch341_tty_install(struct tty_driver *driver,
			     struct tty_struct *tty)
{
	struct ch341 *ch341;
	int retval;

	ch341 = ch341_get_by_index(tty->index);
	if (!ch341)
		return -ENODEV;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	retval = tty_standard_install(driver, tty);
	if (retval)
		goto error_init_termios;

	tty->driver_data = ch341;
#else
	retval = tty_init_termios(tty);
	if (retval)
		goto error_init_termios;

	tty->driver_data = ch341;

	/* Final install (we use the default method) */
	tty_driver_kref_get(driver);
	tty->count++;
	driver->ttys[tty->index] = tty;
#endif
	return 0;

error_init_termios:
	tty_port_put(&ch341->port);
	return retval;
}

static int ch341_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct ch341 *ch341 = tty->driver_data;

	return tty_port_open(&ch341->port, tty, filp);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
static void ch341_port_dtr_rts(struct tty_port *port, bool raise)
#else
static void ch341_port_dtr_rts(struct tty_port *port, int raise)
#endif
{
	struct ch341 *ch341 = container_of(port, struct ch341, port);
	int res;

	if (raise)
		ch341->ctrlout |= CH341_CTO_D | CH341_CTO_R;
	else
		ch341->ctrlout &= ~(CH341_CTO_D | CH341_CTO_R);
	if (ch341->hardflow)
		ch341->ctrlout |= CH341_CTO_R;
	res = ch341_set_control(ch341, ch341->ctrlout);
	if (res)
		dev_err(&ch341->data->dev, "failed to set dtr/rts\n");
}

static int ch341_port_activate(struct tty_port *port,
			       struct tty_struct *tty)
{
	struct ch341 *ch341 = container_of(port, struct ch341, port);
	int retval = -ENODEV;
	int i;

	mutex_lock(&ch341->mutex);
	if (ch341->disconnected)
		goto disconnected;

	retval = usb_autopm_get_interface(ch341->data);
	if (retval)
		goto error_get_interface;

	set_bit(TTY_NO_WRITE_SPLIT, &tty->flags);
	ch341->data->needs_remote_wakeup = 1;
	retval = ch341_configure(ch341);
	if (retval)
		goto error_configure;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
	ch341_tty_set_termios(tty, NULL);
#endif

	retval = usb_submit_urb(ch341->ctrlurb, GFP_KERNEL);
	if (retval) {
		dev_err(&ch341->data->dev,
			"%s - usb_submit_urb(ctrl cmd) failed\n",
			__func__);
		goto error_submit_urb;
	}

	retval = ch341_submit_read_urbs(ch341, GFP_KERNEL);
	if (retval)
		goto error_submit_read_urbs;
	usb_autopm_put_interface(ch341->data);

	mutex_unlock(&ch341->mutex);

	return 0;

error_submit_read_urbs:
	for (i = 0; i < ch341->rx_buflimit; i++)
		usb_kill_urb(ch341->read_urbs[i]);
error_submit_urb:
	usb_kill_urb(ch341->ctrlurb);
error_configure:
	usb_autopm_put_interface(ch341->data);
error_get_interface:
disconnected:
	mutex_unlock(&ch341->mutex);

	return usb_translate_errors(retval);
}

static void ch341_port_destruct(struct tty_port *port)
{
	struct ch341 *ch341 = container_of(port, struct ch341, port);

	ch341_release_minor(ch341);
	usb_put_intf(ch341->data);
	kfree(ch341);
}

static void ch341_port_shutdown(struct tty_port *port)
{
	struct ch341 *ch341 = container_of(port, struct ch341, port);
	struct urb *urb;
	struct ch341_wb *wb;
	int i;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	usb_autopm_get_interface_no_resume(ch341->data);
	ch341->data->needs_remote_wakeup = 0;
	usb_autopm_put_interface(ch341->data);

	for (;;) {
		urb = usb_get_from_anchor(&ch341->delayed);
		if (!urb)
			break;
		wb = urb->context;
		wb->use = 0;
		usb_autopm_put_interface_async(ch341->data);
	}

	usb_kill_urb(ch341->ctrlurb);
	for (i = 0; i < CH341_NW; i++)
		usb_kill_urb(ch341->wb[i].urb);
	for (i = 0; i < ch341->rx_buflimit; i++)
		usb_kill_urb(ch341->read_urbs[i]);
#else
	mutex_lock(&ch341->mutex);
	if (!ch341->disconnected) {
		usb_autopm_get_interface(ch341->data);
		ch341_set_control(ch341, ch341->ctrlout = 0);

		usb_kill_urb(ch341->ctrlurb);
		for (i = 0; i < CH341_NW; i++)
			usb_kill_urb(ch341->wb[i].urb);
		for (i = 0; i < ch341->rx_buflimit; i++)
			usb_kill_urb(ch341->read_urbs[i]);
		ch341->data->needs_remote_wakeup = 0;

		usb_autopm_put_interface(ch341->data);
	}
	mutex_unlock(&ch341->mutex);
#endif
}

static void ch341_tty_cleanup(struct tty_struct *tty)
{
	struct ch341 *ch341 = tty->driver_data;

	tty_port_put(&ch341->port);
}

static void ch341_tty_hangup(struct tty_struct *tty)
{
	struct ch341 *ch341 = tty->driver_data;

	tty_port_hangup(&ch341->port);
}

static void ch341_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct ch341 *ch341 = tty->driver_data;

	tty_port_close(&ch341->port, tty, filp);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static ssize_t ch341_tty_write(struct tty_struct *tty, const u8 *buf,
			       size_t count)
#else
static int ch341_tty_write(struct tty_struct *tty,
			   const unsigned char *buf, int count)
#endif
{
	struct ch341 *ch341 = tty->driver_data;
	int stat;
	unsigned long flags;
	int wbn;
	struct ch341_wb *wb;

	if (!count)
		return 0;

	spin_lock_irqsave(&ch341->write_lock, flags);
	wbn = ch341_wb_alloc(ch341);
	if (wbn < 0) {
		spin_unlock_irqrestore(&ch341->write_lock, flags);
		return 0;
	}
	wb = &ch341->wb[wbn];

	if (!ch341->dev) {
		wb->use = 0;
		spin_unlock_irqrestore(&ch341->write_lock, flags);
		return -ENODEV;
	}

	count = (count > ch341->writesize) ? ch341->writesize : count;
	memcpy(wb->buf, buf, count);
	wb->len = count;

	stat = usb_autopm_get_interface_async(ch341->data);
	if (stat) {
		wb->use = 0;
		spin_unlock_irqrestore(&ch341->write_lock, flags);
		return stat;
	}

	if (ch341->susp_count) {
		usb_anchor_urb(wb->urb, &ch341->delayed);
		spin_unlock_irqrestore(&ch341->write_lock, flags);
		return count;
	}

	stat = ch341_start_wb(ch341, wb);
	spin_unlock_irqrestore(&ch341->write_lock, flags);

	if (stat < 0)
		return stat;
	return count;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0))
static unsigned int ch341_tty_write_room(struct tty_struct *tty)
#else
static int ch341_tty_write_room(struct tty_struct *tty)
#endif
{
	struct ch341 *ch341 = tty->driver_data;

	return ch341_wb_is_avail(ch341) ? ch341->writesize : 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0))
static unsigned int ch341_tty_chars_in_buffer(struct tty_struct *tty)
#else
static int ch341_tty_chars_in_buffer(struct tty_struct *tty)
#endif
{
	struct ch341 *ch341 = tty->driver_data;

	if (ch341->disconnected)
		return 0;

	return (CH341_NW - ch341_wb_is_avail(ch341)) * ch341->writesize;
}

static int ch341_tty_break_ctl(struct tty_struct *tty, int state)
{
	struct ch341 *ch341 = tty->driver_data;
	int retval;
	uint16_t reg_contents;
	uint8_t *regbuf;
	const u16 regval = ((u16)CH341_RL << 8) | CH341_RB;

	regbuf = kmalloc(2, GFP_KERNEL);
	if (!regbuf)
		return -1;

	retval = ch341_control_in(ch341, CMD_R, regval, 0, regbuf, 2);
	if (retval < 0) {
		dev_err(&ch341->data->dev,
			"%s - ch341_control_in error (%d)\n", __func__,
			retval);
		goto out;
	}
	if (state != 0) {
		regbuf[0] &= ~CH341_NB;
		regbuf[1] &= ~CH341_L_ET;
	} else {
		regbuf[0] |= CH341_NB;
		regbuf[1] |= CH341_L_ET;
	}
	reg_contents = get_unaligned_le16(regbuf);

	retval = ch341_control_out(ch341, CMD_W, regval, reg_contents);
	if (retval < 0)
		dev_err(&ch341->data->dev,
			"%s - ch341_control_in error (%d)\n", __func__,
			retval);
out:
	kfree(regbuf);
	return retval;
}

static int ch341_tty_tiocmget(struct tty_struct *tty)
{
	struct ch341 *ch341 = tty->driver_data;
	unsigned long flags;
	unsigned int result;

	spin_lock_irqsave(&ch341->read_lock, flags);
	result = (ch341->ctrlout & CH341_CTO_D ? TIOCM_DTR : 0) |
		 (ch341->ctrlout & CH341_CTO_R ? TIOCM_RTS : 0) |
		 (ch341->ctrlin & CH341_CTI_C ? TIOCM_CTS : 0) |
		 (ch341->ctrlin & CH341_CTI_DS ? TIOCM_DSR : 0) |
		 (ch341->ctrlin & CH341_CTRL_RI ? TIOCM_RI : 0) |
		 (ch341->ctrlin & CH341_CTI_DC ? TIOCM_CD : 0);
	spin_unlock_irqrestore(&ch341->read_lock, flags);

	return result;
}

static int ch341_tty_tiocmset(struct tty_struct *tty, unsigned int set,
			      unsigned int clear)
{
	struct ch341 *ch341 = tty->driver_data;
	unsigned int newctrl;

	newctrl = ch341->ctrlout;
	set = (set & TIOCM_DTR ? CH341_CTO_D : 0) |
	      (set & TIOCM_RTS ? CH341_CTO_R : 0);
	clear = (clear & TIOCM_DTR ? CH341_CTO_D : 0) |
		(clear & TIOCM_RTS ? CH341_CTO_R : 0);
	newctrl = (newctrl & ~clear) | set;
	if (C_CRTSCTS(tty))
		newctrl |= CH341_CTO_R;
	if (ch341->ctrlout == newctrl)
		return 0;

	return ch341_set_control(ch341, ch341->ctrlout = newctrl);
}

static int ch341_get_icount(struct tty_struct *tty,
			    struct serial_icounter_struct *icount)
{
	struct ch341 *ch341 = tty->driver_data;
	struct async_icount cnow;
	unsigned long flags;

	spin_lock_irqsave(&ch341->read_lock, flags);
	cnow = ch341->iocount;
	spin_unlock_irqrestore(&ch341->read_lock, flags);

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->tx = cnow.tx;
	icount->rx = cnow.rx;
	icount->frame = cnow.frame;
	icount->parity = cnow.parity;
	icount->overrun = cnow.overrun;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}

static int get_serial_info(struct ch341 *ch341,
			   struct serial_struct __user *info)
{
	struct serial_struct tmp;

	if (!info)
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	tmp.flags = ASYNC_LOW_LATENCY;
	tmp.xmit_fifo_size = ch341->writesize;
	tmp.baud_base = le32_to_cpu(ch341->line.dwDTERate);
	tmp.close_delay = ch341->port.close_delay / 10;
	tmp.closing_wait = ch341->port.closing_wait ==
					   ASYNC_CLOSING_WAIT_NONE ?
				   ASYNC_CLOSING_WAIT_NONE :
				   ch341->port.closing_wait / 10;
	if (copy_to_user(info, &tmp, sizeof(tmp)))
		return -EFAULT;
	else
		return 0;
}

static int set_serial_info(struct ch341 *ch341,
			   struct serial_struct __user *newinfo)
{
	struct serial_struct new_serial;
	unsigned int closing_wait, close_delay;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	close_delay = new_serial.close_delay * 10;
	closing_wait = new_serial.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			       ASYNC_CLOSING_WAIT_NONE :
			       new_serial.closing_wait * 10;

	mutex_lock(&ch341->port.mutex);
	if (!capable(CAP_SYS_ADMIN)) {
		if ((close_delay != ch341->port.close_delay) ||
		    (closing_wait != ch341->port.closing_wait))
			retval = -EPERM;
		else
			retval = -EOPNOTSUPP;
	} else {
		ch341->port.close_delay = close_delay;
		ch341->port.closing_wait = closing_wait;
	}
	mutex_unlock(&ch341->port.mutex);

	return retval;
}

static int wait_serial_change(struct ch341 *ch341, unsigned long arg)
{
	int rv = 0;
	DECLARE_WAITQUEUE(wait, current);
	struct async_icount old, new;

	do {
		spin_lock_irq(&ch341->read_lock);
		old = ch341->oldcount;
		new = ch341->iocount;
		ch341->oldcount = new;
		spin_unlock_irq(&ch341->read_lock);

		if ((arg & TIOCM_CTS) && old.cts != new.cts)
			break;
		if ((arg & TIOCM_DSR) && old.dsr != new.dsr)
			break;
		if ((arg & TIOCM_RI) && old.rng != new.rng)
			break;
		if ((arg & TIOCM_CD) && old.dcd != new.dcd)
			break;

		add_wait_queue(&ch341->wioctl, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		remove_wait_queue(&ch341->wioctl, &wait);
		if (ch341->disconnected) {
			rv = -ENODEV;
		} else {
			if (signal_pending(current))
				rv = -ERESTARTSYS;
		}
	} while (!rv);

	return rv;
}

static int get_serial_usage(struct ch341 *ch341,
			    struct serial_icounter_struct __user *count)
{
	struct serial_icounter_struct icount;
	int rv = 0;

	memset(&icount, 0, sizeof(icount));
	icount.cts = ch341->iocount.cts;
	icount.dsr = ch341->iocount.dsr;
	icount.rng = ch341->iocount.rng;
	icount.dcd = ch341->iocount.dcd;
	icount.tx = ch341->iocount.tx;
	icount.rx = ch341->iocount.rx;
	icount.frame = ch341->iocount.frame;
	icount.overrun = ch341->iocount.overrun;
	icount.parity = ch341->iocount.parity;
	icount.brk = ch341->iocount.brk;
	icount.buf_overrun = ch341->iocount.buf_overrun;

	if (copy_to_user(count, &icount, sizeof(icount)) > 0)
		rv = -EFAULT;

	return rv;
}

static int ch341_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
			   unsigned long arg)
{
	struct ch341 *ch341 = tty->driver_data;
	int rv = -ENOIOCTLCMD;

	switch (cmd) {
	case TIOCGSERIAL: /* gets serial port data */
		rv = get_serial_info(ch341,
				     (struct serial_struct __user *)arg);
		break;
	case TIOCSSERIAL:
		rv = set_serial_info(ch341,
				     (struct serial_struct __user *)arg);
		break;
	case TIOCMIWAIT:
		rv = usb_autopm_get_interface(ch341->data);
		if (rv < 0) {
			rv = -EIO;
			break;
		}
		rv = wait_serial_change(ch341, arg);
		usb_autopm_put_interface(ch341->data);
		break;
	case TIOCGICOUNT:
		rv = get_serial_usage(
			ch341,
			(struct serial_icounter_struct __user *)arg);
		break;
	}

	return rv;
}

static int ch341_get(unsigned int baval, unsigned char *factor,
		     unsigned char *divisor)
{
	unsigned char a;
	unsigned char b;
	unsigned long c;

	switch (baval) {
	case 921600:
		a = 0xf3;
		b = 7;
		break;
	case 307200:
		a = 0xd9;
		b = 7;
		break;
	default:
		if (baval > 6000000 / 255) {
			b = 3;
			c = 6000000;
		} else if (baval > 750000 / 255) {
			b = 2;
			c = 750000;
		} else if (baval > 93750 / 255) {
			b = 1;
			c = 93750;
		} else {
			b = 0;
			c = 11719;
		}
		a = (unsigned char)(c / baval);
		if (a == 0 || a == 0xFF)
			return -EINVAL;
		if ((c / a - baval) > (baval - c / (a + 1)))
			a++;
		a = 256 - a;
		break;
	}
	*factor = a;
	*divisor = b;

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
static void ch341_tty_set_termios(struct tty_struct *tty,
				  const struct ktermios *termios_old)
#else
static void ch341_tty_set_termios(struct tty_struct *tty,
				  struct ktermios *termios_old)
#endif
{
	struct ch341 *ch341 = tty->driver_data;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	struct ktermios *termios = &tty->termios;
#else
	struct ktermios *termios = tty->termios;
#endif
	struct usb_ch341_line_coding newline;
	int newctrl = ch341->ctrlout;

	unsigned char divisor = 0;
	unsigned char reg_count = 0;
	unsigned char factor = 0;
	unsigned char reg_value = 0;
	unsigned short value = 0;
	unsigned short index = 0;
	unsigned char timeout = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	if (termios_old &&
	    !tty_termios_hw_change(&tty->termios, termios_old)) {
#else
	if (termios_old &&
	    !tty_termios_hw_change(tty->termios, termios_old)) {
#endif
		return;
	}

	newline.dwDTERate = tty_get_baud_rate(tty);

	if (newline.dwDTERate == 0)
		newline.dwDTERate = 9600;
	ch341_get(newline.dwDTERate, &factor, &divisor);

	newline.bCharFormat = termios->c_cflag & CSTOPB ? 2 : 1;
	if (newline.bCharFormat == 2)
		reg_value |= CH341_L_SB;

	newline.bParityType =
		termios->c_cflag & PARENB ?
			(termios->c_cflag & PARODD ? 1 : 2) +
				(termios->c_cflag & CMSPAR ? 2 : 0) :
			0;

	switch (newline.bParityType) {
	case 0x01:
		reg_value |= CH341_L_PO;
		break;
	case 0x02:
		reg_value |= CH341_L_PE;
		break;
	case 0x03:
		reg_value |= CH341_L_PM;
		break;
	case 0x04:
		reg_value |= CH341_L_PS;
		break;
	default:
		break;
	}

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		newline.bDataBits = 5;
		reg_value |= CH341_L_D5;
		break;
	case CS6:
		newline.bDataBits = 6;
		reg_value |= CH341_L_D6;
		break;
	case CS7:
		newline.bDataBits = 7;
		reg_value |= CH341_L_D7;
		break;
	case CS8:
	default:
		newline.bDataBits = 8;
		reg_value |= CH341_L_D8;
		break;
	}

	ch341->clocal = ((termios->c_cflag & CLOCAL) != 0);

	if (C_BAUD(tty) == B0) {
		newline.dwDTERate = ch341->line.dwDTERate;
		newctrl &= ~CH341_CTO_D;
	} else if (termios_old && (termios_old->c_cflag & CBAUD) == B0) {
		newctrl |= CH341_CTO_D;
	}

	reg_value |= 0xc0;
	reg_count |= 0x9c;

	value |= reg_count;
	value |= (unsigned short)reg_value << 8;
	index |= 0x80 | divisor;
	index |= (unsigned short)factor << 8;
	ch341_control_out(ch341, CMD_C1, value, index);

	timeout = 76800 / newline.dwDTERate;
	if (timeout < 0x07)
		timeout = 0x07;
	ch341_control_out(ch341, CMD_W, 0x0f2c, timeout);

	if (newctrl != ch341->ctrlout)
		ch341_set_control(ch341, ch341->ctrlout = newctrl);

	if (memcmp(&ch341->line, &newline, sizeof newline))
		memcpy(&ch341->line, &newline, sizeof newline);
	if (C_CRTSCTS(tty)) {
		ch341_control_out(ch341, CMD_W, 0x2727, 0x0101);
		newctrl |= CH341_CTO_R;
		ch341_set_control(ch341, ch341->ctrlout = newctrl);
		ch341->hardflow = true;
	} else {
		ch341_control_out(ch341, CMD_W, 0x2727, 0x0000);
		ch341->hardflow = false;
	}
}

static const struct tty_port_operations ch341_port_ops = {
	.dtr_rts = ch341_port_dtr_rts,
	.shutdown = ch341_port_shutdown,
	.activate = ch341_port_activate,
	.destruct = ch341_port_destruct,
};

/*
 * USB probe and disconnect routines.
 */
static void ch341_write_buffers_free(struct ch341 *ch341)
{
	int i;
	struct ch341_wb *wb;
	struct usb_device *usb_dev = interface_to_usbdev(ch341->data);

	for (wb = &ch341->wb[0], i = 0; i < CH341_NW; i++, wb++)
		usb_free_coherent(usb_dev, ch341->writesize, wb->buf,
				  wb->dmah);
}

static void ch341_read_buffers_free(struct ch341 *ch341)
{
	struct usb_device *usb_dev = interface_to_usbdev(ch341->data);
	int i;

	for (i = 0; i < ch341->rx_buflimit; i++)
		usb_free_coherent(usb_dev, ch341->readsize,
				  ch341->read_buffers[i].base,
				  ch341->read_buffers[i].dma);
}

static int ch341_write_buffers_alloc(struct ch341 *ch341)
{
	int i;
	struct ch341_wb *wb;

	for (wb = &ch341->wb[0], i = 0; i < CH341_NW; i++, wb++) {
		wb->buf = usb_alloc_coherent(ch341->dev, ch341->writesize,
					     GFP_KERNEL, &wb->dmah);
		if (!wb->buf) {
			while (i != 0) {
				--i;
				--wb;
				usb_free_coherent(ch341->dev,
						  ch341->writesize,
						  wb->buf, wb->dmah);
			}
			return -ENOMEM;
		}
	}
	return 0;
}

static int ch341_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epctrl = NULL;
	struct usb_endpoint_descriptor *epread = NULL;
	struct usb_endpoint_descriptor *epwrite = NULL;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct ch341 *ch341;
	int minor;
	int ctrlsize, readsize;
	u8 *buf;
	unsigned long quirks;
	int num_rx_buf = CH341_NR;
	int i;
	struct device *tty_dev;
	int rv = -ENOMEM;

	/* normal quirks */
	quirks = (unsigned long)id->driver_info;

	data_interface = usb_ifnum_to_if(usb_dev, 0);

	if (intf != data_interface)
		return -ENODEV;

	if (data_interface->cur_altsetting->desc.bNumEndpoints < 2 ||
	    data_interface->cur_altsetting->desc.bNumEndpoints == 0)
		return -EINVAL;

	epread = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;
	epctrl = &data_interface->cur_altsetting->endpoint[2].desc;

	if (!usb_endpoint_dir_in(epread)) {
		swap(epread, epwrite);
	}

	ch341 = kzalloc(sizeof(struct ch341), GFP_KERNEL);
	if (ch341 == NULL)
		goto alloc_fail;

	minor = ch341_alloc_minor(ch341);
	if (minor < 0) {
		dev_err(&intf->dev, "no more free ch341 devices\n");
		kfree(ch341);
		return -ENODEV;
	}

	ctrlsize = usb_endpoint_maxp(epctrl);
	readsize = usb_endpoint_maxp(epread) *
		   (quirks == SINGLE_RX_URB ? 1 : 1);
	ch341->writesize = usb_endpoint_maxp(epwrite) * 20;
	ch341->data = data_interface;
	ch341->minor = minor;
	ch341->dev = usb_dev;
	ch341->ctrlsize = ctrlsize;
	ch341->readsize = readsize;
	ch341->rx_buflimit = num_rx_buf;

	INIT_WORK(&ch341->work, ch341_softint);
	init_waitqueue_head(&ch341->wioctl);
	spin_lock_init(&ch341->write_lock);
	spin_lock_init(&ch341->read_lock);
	mutex_init(&ch341->mutex);
	ch341->rx_endpoint =
		usb_rcvbulkpipe(usb_dev, epread->bEndpointAddress);
	tty_port_init(&ch341->port);
	ch341->port.ops = &ch341_port_ops;
	init_usb_anchor(&ch341->delayed);
	ch341->quirks = quirks;

	buf = usb_alloc_coherent(usb_dev, ctrlsize, GFP_KERNEL,
				 &ch341->ctrl_dma);
	if (!buf)
		goto alloc_fail2;
	ch341->ctrl_buffer = buf;

	if (ch341_write_buffers_alloc(ch341) < 0)
		goto alloc_fail4;

	ch341->ctrlurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ch341->ctrlurb)
		goto alloc_fail5;

	for (i = 0; i < num_rx_buf; i++) {
		struct ch341_rb *rb = &(ch341->read_buffers[i]);
		struct urb *urb;

		rb->base = usb_alloc_coherent(ch341->dev, readsize,
					      GFP_KERNEL, &rb->dma);
		if (!rb->base)
			goto alloc_fail6;
		rb->index = i;
		rb->instance = ch341;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto alloc_fail6;

		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = rb->dma;
		usb_fill_bulk_urb(urb, ch341->dev, ch341->rx_endpoint,
				  rb->base, ch341->readsize,
				  ch341_read_bulk_callback, rb);

		ch341->read_urbs[i] = urb;
		__set_bit(i, &ch341->read_urbs_free);
	}
	for (i = 0; i < CH341_NW; i++) {
		struct ch341_wb *snd = &(ch341->wb[i]);

		snd->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (snd->urb == NULL)
			goto alloc_fail7;

		usb_fill_bulk_urb(
			snd->urb, usb_dev,
			usb_sndbulkpipe(usb_dev,
					epwrite->bEndpointAddress),
			NULL, ch341->writesize, ch341_write_bulk, snd);
		snd->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		snd->instance = ch341;
	}

	usb_set_intfdata(intf, ch341);

	usb_fill_int_urb(ch341->ctrlurb, usb_dev,
			 usb_rcvintpipe(usb_dev, epctrl->bEndpointAddress),
			 ch341->ctrl_buffer, ctrlsize, ch341_ctrl_irq,
			 ch341,
			 epctrl->bInterval ? epctrl->bInterval : 16);
	ch341->ctrlurb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	ch341->ctrlurb->transfer_dma = ch341->ctrl_dma;

	dev_info(&intf->dev, "ttyCH341USB%d: ch341 USB device\n", minor);

	usb_driver_claim_interface(&ch341_driver, data_interface, ch341);
	usb_set_intfdata(data_interface, ch341);

	usb_get_intf(data_interface);
	ch341->line.dwDTERate = 9600;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	tty_dev = tty_port_register_device(&ch341->port, ch341_tty_driver,
					   minor, &data_interface->dev);
	if (IS_ERR(tty_dev)) {
		rv = PTR_ERR(tty_dev);
		goto alloc_fail7;
	}
#else
	tty_register_device(ch341_tty_driver, minor, &data_interface->dev);
#endif
	return 0;

alloc_fail7:
	usb_set_intfdata(intf, NULL);
	for (i = 0; i < CH341_NW; i++)
		usb_free_urb(ch341->wb[i].urb);
alloc_fail6:
	for (i = 0; i < num_rx_buf; i++)
		usb_free_urb(ch341->read_urbs[i]);
	ch341_read_buffers_free(ch341);
	usb_free_urb(ch341->ctrlurb);
alloc_fail5:
	ch341_write_buffers_free(ch341);
alloc_fail4:
	usb_free_coherent(usb_dev, ctrlsize, ch341->ctrl_buffer,
			  ch341->ctrl_dma);
alloc_fail2:
	ch341_release_minor(ch341);
	kfree(ch341);
alloc_fail:
	return rv;
}

static void stop_data_traffic(struct ch341 *ch341)
{
	int i;

	usb_kill_urb(ch341->ctrlurb);
	for (i = 0; i < CH341_NW; i++)
		usb_kill_urb(ch341->wb[i].urb);
	for (i = 0; i < ch341->rx_buflimit; i++)
		usb_kill_urb(ch341->read_urbs[i]);

	cancel_work_sync(&ch341->work);
}

static void ch341_disconnect(struct usb_interface *intf)
{
	struct ch341 *ch341 = usb_get_intfdata(intf);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct tty_struct *tty;
	int i;

	if (!ch341)
		return;
	mutex_lock(&ch341->mutex);
	ch341->disconnected = true;
	wake_up_all(&ch341->wioctl);
	usb_set_intfdata(ch341->data, NULL);
	mutex_unlock(&ch341->mutex);

	tty = tty_port_tty_get(&ch341->port);
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}
	stop_data_traffic(ch341);
	tty_unregister_device(ch341_tty_driver, ch341->minor);

	usb_free_urb(ch341->ctrlurb);
	for (i = 0; i < CH341_NW; i++)
		usb_free_urb(ch341->wb[i].urb);
	for (i = 0; i < ch341->rx_buflimit; i++)
		usb_free_urb(ch341->read_urbs[i]);
	ch341_write_buffers_free(ch341);
	usb_free_coherent(usb_dev, ch341->ctrlsize, ch341->ctrl_buffer,
			  ch341->ctrl_dma);
	ch341_read_buffers_free(ch341);
	usb_driver_release_interface(&ch341_driver, ch341->data);
	tty_port_put(&ch341->port);
	dev_info(&intf->dev, "%s\n", "ch341 usb device disconnect.");
}

#ifdef CONFIG_PM
static int ch341_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct ch341 *ch341 = usb_get_intfdata(intf);
	int cnt;

	spin_lock_irq(&ch341->write_lock);
	if (PMSG_IS_AUTO(message)) {
		if (ch341->transmitting) {
			spin_unlock_irq(&ch341->write_lock);
			return -EBUSY;
		}
	}
	cnt = ch341->susp_count++;
	spin_unlock_irq(&ch341->write_lock);
	if (cnt)
		return 0;

	stop_data_traffic(ch341);

	return 0;
}

static int ch341_resume(struct usb_interface *intf)
{
	struct ch341 *ch341 = usb_get_intfdata(intf);
	struct urb *urb;
	int rv = 0;

	spin_lock_irq(&ch341->write_lock);
	if (--ch341->susp_count)
		goto out;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
	if (tty_port_initialized(&ch341->port)) {
#else
	if (test_bit(ASYNCB_INITIALIZED, &ch341->port.flags)) {
#endif
		rv = usb_submit_urb(ch341->ctrlurb, GFP_ATOMIC);
		for (;;) {
			urb = usb_get_from_anchor(&ch341->delayed);
			if (!urb)
				break;

			ch341_start_wb(ch341, urb->context);
		}
		if (rv < 0)
			goto out;
		rv = ch341_submit_read_urbs(ch341, GFP_ATOMIC);
	}
out:
	spin_unlock_irq(&ch341->write_lock);
	return rv;
}

static int ch341_reset_resume(struct usb_interface *intf)
{
	struct ch341 *ch341 = usb_get_intfdata(intf);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
	if (tty_port_initialized(&ch341->port))
#else
	if (test_bit(ASYNCB_INITIALIZED, &ch341->port.flags))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
		struct tty_struct *tty = tty_port_tty_get(&ch341->port);
	tty_hangup(tty);
#else
		tty_port_tty_hangup(&ch341->port, false);
#endif
	return ch341_resume(intf);
}
#endif /* CONFIG_PM */

/*
 * USB driver structure.
 */
static const struct usb_device_id ch341_ids[] = {
	{ USB_DEVICE(0x1a86, 0x7523) }, /* ch340 chip */
	{ USB_DEVICE(0x1a86, 0x7522) }, /* ch340k chip */
	{ USB_DEVICE(0x1a86, 0x5523) }, /* ch341 chip */
	{ USB_DEVICE(0x1a86, 0xe523) }, /* ch330 chip */
	{ USB_DEVICE(0x4348, 0x5523) }, /* ch340 custom chip */
	{}
};

MODULE_DEVICE_TABLE(usb, ch341_ids);

static struct usb_driver ch341_driver = {
	.name = "usb_ch341",
	.probe = ch341_probe,
	.disconnect = ch341_disconnect,
#ifdef CONFIG_PM
	.suspend = ch341_suspend,
	.resume = ch341_resume,
	.reset_resume = ch341_reset_resume,
#endif
	.id_table = ch341_ids,
#ifdef CONFIG_PM
	.supports_autosuspend = 1,
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 4, 0))
	.disable_hub_initiated_lpm = 1,
#endif
};

/*
 * TTY driver structures.
 */
static const struct tty_operations ch341_ops = {
	.install = ch341_tty_install,
	.open = ch341_tty_open,
	.close = ch341_tty_close,
	.cleanup = ch341_tty_cleanup,
	.hangup = ch341_tty_hangup,
	.write = ch341_tty_write,
	.write_room = ch341_tty_write_room,
	.ioctl = ch341_tty_ioctl,
	.chars_in_buffer = ch341_tty_chars_in_buffer,
	.break_ctl = ch341_tty_break_ctl,
	.set_termios = ch341_tty_set_termios,
	.tiocmget = ch341_tty_tiocmget,
	.tiocmset = ch341_tty_tiocmset,
	.get_icount = ch341_get_icount,
};

static int __init ch341_init(void)
{
	int retval;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	ch341_tty_driver = tty_alloc_driver(
		CH341_TTY_MINORS,
		TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(ch341_tty_driver))
		return PTR_ERR(ch341_tty_driver);
#else
	ch341_tty_driver = alloc_tty_driver(CH341_TTY_MINORS);
	if (!ch341_tty_driver)
		return -ENOMEM;
#endif
	ch341_tty_driver->driver_name = "ch341_uart",
	ch341_tty_driver->name = "ttyCH341USB",
	ch341_tty_driver->major = CH341_TTY_MAJOR,
	ch341_tty_driver->minor_start = 0,
	ch341_tty_driver->type = TTY_DRIVER_TYPE_SERIAL,
	ch341_tty_driver->subtype = SERIAL_TYPE_NORMAL,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0))
	ch341_tty_driver->flags = TTY_DRIVER_REAL_RAW |
				  TTY_DRIVER_DYNAMIC_DEV;
#endif
	ch341_tty_driver->init_termios = tty_std_termios;
	ch341_tty_driver->init_termios.c_cflag = B0 | CS8 | CREAD | HUPCL |
						 CLOCAL;
	tty_set_operations(ch341_tty_driver, &ch341_ops);

	retval = tty_register_driver(ch341_tty_driver);
	if (retval) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		tty_driver_kref_put(ch341_tty_driver);
#else
		put_tty_driver(ch341_tty_driver);
#endif
		return retval;
	}

	retval = usb_register(&ch341_driver);
	if (retval) {
		tty_unregister_driver(ch341_tty_driver);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		tty_driver_kref_put(ch341_tty_driver);
#else
		put_tty_driver(ch341_tty_driver);
#endif
		return retval;
	}

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");
	printk(KERN_INFO KBUILD_MODNAME ": " VERSION_DESC "\n");

	return 0;
}

static void __exit ch341_exit(void)
{
	usb_deregister(&ch341_driver);
	tty_unregister_driver(ch341_tty_driver);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	tty_driver_kref_put(ch341_tty_driver);
#else
	put_tty_driver(ch341_tty_driver);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	idr_destroy(&ch341_minors);
#endif
	printk(KERN_INFO KBUILD_MODNAME ": "
					"ch341 driver exit.\n");
}

module_init(ch341_init);
module_exit(ch341_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(VERSION_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(CH341_TTY_MAJOR);
