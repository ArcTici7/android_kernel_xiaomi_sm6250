#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac2.h"

/*
 * TARGET: stable UAC2 device
 * - no async tricks
 * - no feedback endpoint
 * - fixed packet sizes
 */

#define HS_PKT 192   /* 48kHz * 2ch * 2 bytes / 1000ms */
#define FS_PKT 192

/* FULL SPEED OUT */
static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.bInterval = 1,
};

/* FULL SPEED IN */
static struct usb_endpoint_descriptor fs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.bInterval = 1,
};

/* HIGH SPEED OUT */
static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.wMaxPacketSize = cpu_to_le16(HS_PKT),
	.bInterval = 4,
};

/* HIGH SPEED IN */
static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.wMaxPacketSize = cpu_to_le16(HS_PKT),
	.bInterval = 4,
};

/* FIXED packet sizing (NO runtime math) */
static int set_ep_max_packet_size(const struct f_uac2_opts *opts,
	struct usb_endpoint_descriptor *ep,
	enum usb_device_speed speed, bool playback)
{
	if (speed == USB_SPEED_HIGH)
		ep->wMaxPacketSize = cpu_to_le16(HS_PKT);
	else
		ep->wMaxPacketSize = cpu_to_le16(FS_PKT);

	return 0;
}

MODULE_LICENSE("GPL");
