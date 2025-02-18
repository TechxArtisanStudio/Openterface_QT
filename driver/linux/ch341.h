/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Includes for ch341.c
 *
 */

#ifndef _CH341_H
#define _CH341_H

/*
 * Baud rate and default timeout
 */
#define DEFAULT_BAUD_RATE 9600
#define DEFAULT_TIMEOUT 2000

/*
 * CMSPAR, some architectures can't have space and mark parity.
 */
#ifndef CMSPAR
#define CMSPAR 0
#endif

/*
 * Major and minor numbers.
 */
#define CH341_TTY_MAJOR 169
#define CH341_TTY_MINORS 256

/*
 * Requests.
 */
#define USB_RT_CH341 (USB_TYPE_CLASS | USB_RECIP_INTERFACE)

#define CMD_R 0x95
#define CMD_W 0x9A
#define CMD_C1 0xA1
#define CMD_C2 0xA4
#define CMD_C3 0x5F

#define CH341_CTO_O 0x10
#define CH341_CTO_D 0x20
#define CH341_CTO_R 0x40
#define CH341_CTI_C 0x01
#define CH341_CTI_DS 0x02
#define CH341_CTRL_RI 0x04
#define CH341_CTI_DC 0x08
#define CH341_CTI_ST 0x0f

#define CH341_CTT_M BIT(3)
#define CH341_CTT_F (BIT(2) | BIT(6))
#define CH341_CTT_P BIT(2)
#define CH341_CTT_O BIT(1)

#define CH341_L_ER 0x80
#define CH341_L_ET 0x40
#define CH341_L_PS 0x38
#define CH341_L_PM 0x28
#define CH341_L_PE 0x18
#define CH341_L_PO 0x08
#define CH341_L_SB 0x04
#define CH341_L_D8 0x03
#define CH341_L_D7 0x02
#define CH341_L_D6 0x01
#define CH341_L_D5 0x00

#define CH341_RB 0x05
#define CH341_RL 0x18
#define CH341_NB 0x01

#define CH341_NW 2
#define CH341_NR 4

struct ch341_wb {
	unsigned char *buf;
	dma_addr_t dmah;
	int len;
	int use;
	struct urb *urb;
	struct ch341 *instance;
};

struct ch341_rb {
	int size;
	unsigned char *base;
	dma_addr_t dma;
	int index;
	struct ch341 *instance;
};

struct usb_ch341_line_coding {
	__le32 dwDTERate;
	__u8 bCharFormat;
#define USB_CH341_1_STOP_BITS 0
#define USB_CH341_1_5_STOP_BITS 1
#define USB_CH341_2_STOP_BITS 2

	__u8 bParityType;
#define USB_CH341_NO_PARITY 0
#define USB_CH341_ODD_PARITY 1
#define USB_CH341_EVEN_PARITY 2
#define USB_CH341_MARK_PARITY 3
#define USB_CH341_SPACE_PARITY 4

	__u8 bDataBits;
} __attribute__((packed));

struct ch341 {
	struct usb_device *dev; /* the corresponding usb device */
	struct usb_interface *data; /* data interface */
	struct tty_port port; /* our tty port data */
	struct urb *ctrlurb; /* urbs */
	u8 *ctrl_buffer; /* buffers of urbs */
	dma_addr_t ctrl_dma; /* dma handles of buffers */
	struct ch341_wb wb[CH341_NW];
	unsigned long read_urbs_free;
	struct urb *read_urbs[CH341_NR];
	struct ch341_rb read_buffers[CH341_NR];
	int rx_buflimit;
	int rx_endpoint;
	spinlock_t read_lock;
	int write_used; /* number of non-empty write buffers */
	int transmitting;
	spinlock_t write_lock;
	struct mutex mutex;
	bool disconnected;
	struct usb_ch341_line_coding line; /* baudrate, data format */
	struct work_struct work; /* used for line discipline waking up */
	unsigned int ctrlin; /* input lines (CTS, DSR, DCD, RI) */
	unsigned int ctrlout; /* output control lines (DTR, RTS) */
	struct async_icount iocount; /* counters for control line changes */
	struct async_icount oldcount; /* for comparison of counter */
	wait_queue_head_t wioctl; /* for ioctl */
	unsigned int writesize; /* max packet size */
	unsigned int readsize, ctrlsize; /* buffer sizes for freeing */
	unsigned int minor; /* ch341 minor number */
	unsigned char clocal; /* termios CLOCAL */
	unsigned int susp_count; /* number of suspended interfaces */
	u8 bInterval;
	struct usb_anchor delayed; /* used for a device about to be woken */
	unsigned long quirks;
	bool hardflow;
};

/* constants describing various quirks and errors */
#define NO_UNION_NORMAL BIT(0)
#define SINGLE_RX_URB BIT(1)
#define NO_CAP_LINE BIT(2)
#define NO_DATA_INTERFACE BIT(4)
#define IGNORE_DEVICE BIT(5)
#define QUIRK_CONTROL_LINE_STATE BIT(6)
#define CLEAR_HALT_CONDITIONS BIT(7)
#endif
