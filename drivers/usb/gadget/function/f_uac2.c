/*
 * f_uac2.c -- USB Audio Class 2.0 Function (STABLE LOW-LATENCY CORE)
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac2.h"

#define UAC2_INTERVAL 1   /* FIXED: keep 1 for HS stability */

/* ===========================
 * ENDPOINT STRATEGY (FIXED)
 * ===========================
 * - Playback: Async OUT + implicit feedback
 * - Capture: standard IN ISO
 */

/* FULL SPEED (unchanged safe) */
static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.bInterval = 1,
};

/* HIGH SPEED (FIXED STABLE MODE) */
static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,

	/*
	 * FIX:
	 * Windows + Android gadget stability sweet spot = 1–2
	 */
	.bInterval = UAC2_INTERVAL,
};

/* IN endpoint */
static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_NONE,
	.bInterval = UAC2_INTERVAL,
};

/* ===========================
 * FIXED FEEDBACK ENDPOINT
 * ===========================
 * IMPORTANT:
 * Must be PRESENT if async OUT is used
 */

static struct usb_endpoint_descriptor fs_ep_fb_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN | 0x03,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC,

	.wMaxPacketSize = cpu_to_le16(3),
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_ep_fb_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN | 0x03,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC,

	.wMaxPacketSize = cpu_to_le16(3),
	.bInterval = 4,
};

/* ===========================
 * FIXED PACKET CALCULATION
 * ===========================
 * REMOVE interval-based jitter math
 */

static int set_ep_max_packet_size(const struct f_uac2_opts *opts,
	struct usb_endpoint_descriptor *ep, enum usb_device_speed speed,
	bool playback)
{
	u16 max_packet;

	int channels = playback ? opts->p_chmask : opts->c_chmask;
	int ssize = playback ? opts->p_ssize : opts->c_ssize;
	int srate = playback ? opts->p_srate : opts->c_srate;

	/*
	 * FIX:
	 * deterministic sizing only
	 * NO interval-based division (causes Windows drift)
	 */
	if (speed == USB_SPEED_HIGH)
		max_packet = (channels * ssize * srate) / 8000;
	else
		max_packet = (channels * ssize * srate) / 1000;

	if (max_packet > 1024)
		max_packet = 1024;

	ep->wMaxPacketSize = cpu_to_le16(max_packet);

	return 0;
}

MODULE_LICENSE("GPL");
