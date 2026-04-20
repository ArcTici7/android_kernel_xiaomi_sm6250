/*
 * f_uac2.c -- USB Audio Class 2.0 Function (STABLE BASELINE)
 *
 * Goal: Windows-compatible, low-latency, no drift hacks, no unstable sync tricks
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac2.h"

#define UAC_MAX_PKT_SIZE_HS   192   /* 2ch * 2bytes * 48k / 1000 */
#define UAC_MAX_PKT_SIZE_FS   192

/* ---------------------------
 * ENDPOINT DESCRIPTORS
 * --------------------------- */

/* FULL SPEED OUT (HOST -> DEVICE) */
static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.bInterval = 1,
};

/* FULL SPEED IN (DEVICE -> HOST) */
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
	.wMaxPacketSize = cpu_to_le16(UAC_MAX_PKT_SIZE_HS),
	.bInterval = 4,
};

/* HIGH SPEED IN */
static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.wMaxPacketSize = cpu_to_le16(UAC_MAX_PKT_SIZE_HS),
	.bInterval = 4,
};

/* ---------------------------
 * FEEDBACK ENDPOINT (DISABLED SAFE BASELINE)
 * --------------------------- */
/*
 * NOTE:
 * We intentionally do NOT enable feedback endpoint logic here.
 * Windows handles adaptive sync fine for this baseline DAC mode.
 */

/* ---------------------------
 * PACKET SIZE FUNCTION (STATIC SAFE)
 * --------------------------- */

static int set_ep_max_packet_size(const struct f_uac2_opts *opts,
	struct usb_endpoint_descriptor *ep,
	enum usb_device_speed speed, bool playback)
{
	u16 pkt;

	if (speed == USB_SPEED_HIGH)
		pkt = UAC_MAX_PKT_SIZE_HS;
	else
		pkt = UAC_MAX_PKT_SIZE_FS;

	ep->wMaxPacketSize = cpu_to_le16(pkt);
	return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stable UAC2 Baseline");
