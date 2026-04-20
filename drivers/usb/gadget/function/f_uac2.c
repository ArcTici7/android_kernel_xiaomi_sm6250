/*
 * f_uac2.c -- USB Audio Class 2.0 Function (MATCHED CORE)
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac2.h"

/* =========================================================
 * ENDPOINTS (MUST MATCH SYNC MODEL)
 * ========================================================= */

/* FULL SPEED */
static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.bInterval = 1,
};

static struct usb_endpoint_descriptor fs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.bInterval = 1,
};

/* HIGH SPEED (STABLE CONFIG) */
static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.bInterval = UAC2_INTERVAL,
};

static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.bInterval = UAC2_INTERVAL,
};

/* =========================================================
 * FEEDBACK ENDPOINT (REQUIRED FOR ASYNC OUT)
 * ========================================================= */

static struct usb_endpoint_descriptor hs_ep_fb_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN | 0x03,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize = cpu_to_le16(3),
	.bInterval = 4,
};

/* =========================================================
 * PACKET SIZE (DETERMINISTIC — NO JITTER FORMULA)
 * ========================================================= */

static int set_ep_max_packet_size(const struct f_uac2_opts *opts,
	struct usb_endpoint_descriptor *ep,
	enum usb_device_speed speed,
	bool playback)
{
	u16 val;

	int ch = UAC2_CHANNELS;
	int ss = UAC2_SAMPLE_SIZE;
	int sr = UAC2_FORMAT_RATE;

	if (speed == USB_SPEED_HIGH)
		val = (ch * ss * sr) / 8000;
	else
		val = (ch * ss * sr) / 1000;

	if (val > UAC2_MAX_PACKET_HS)
		val = UAC2_MAX_PACKET_HS;

	ep->wMaxPacketSize = cpu_to_le16(val);
	return 0;
}

MODULE_LICENSE("GPL");
