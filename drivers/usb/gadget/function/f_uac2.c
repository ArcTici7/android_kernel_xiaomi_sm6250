/*
 * f_uac2.c -- USB Audio Class 2.0 Function
 * STABLE ALSA ↔ USB SYNC BUILD (128-frame aligned)
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac2.h"

/*
 * =========================================================
 * FIXED AUDIO PIPELINE TARGET
 * =========================================================
 * ALSA period: 128 frames
 * 48kHz stereo 16-bit => 512 bytes per period
 * USB must always transport 512-byte aligned blocks
 * =========================================================
 */

#define USB_AUDIO_PACKET_SIZE 512

/*
 * =========================================================
 * ENDPOINTS (SYNC STABLE CONFIG)
 * =========================================================
 */

/* FULL SPEED OUT */
static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,

	.bInterval = 1,
};

/* HIGH SPEED OUT (MAIN FIX) */
static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,

	/* ISO + SYNC mode (important for Windows stability) */
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,

	/*
	 * KEY FIX:
	 * Must match 512-byte ALSA period transport
	 * NOT oversampled interval tricks
	 */
	.wMaxPacketSize = cpu_to_le16(USB_AUDIO_PACKET_SIZE),

	/*
	 * FIXED MICROFRAME SCHEDULING
	 * 4 = stable USB HS interval (~1ms base pacing)
	 */
	.bInterval = 4,
};

/* FULL SPEED IN */
static struct usb_endpoint_descriptor fs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,
	.bInterval = 1,
};

/* HIGH SPEED IN (MAIN FIX) */
static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,

	.wMaxPacketSize = cpu_to_le16(USB_AUDIO_PACKET_SIZE),
	.bInterval = 4,
};

/*
 * =========================================================
 * OPTIONAL FEEDBACK ENDPOINT (DISABLED SAFE STUB)
 * =========================================================
 * DO NOT ENABLE unless doing async clock recovery tuning
 * =========================================================
 */

static struct usb_endpoint_descriptor hs_ep_fb_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN | 0x03,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC,

	.wMaxPacketSize = cpu_to_le16(3),
	.bInterval = 4,
};

/*
 * =========================================================
 * PACKET SIZE OVERRIDE (HARD SYNC ENFORCEMENT)
 * =========================================================
 */

static int set_ep_max_packet_size(const struct f_uac2_opts *opts,
	struct usb_endpoint_descriptor *ep_desc,
	enum usb_device_speed speed, bool is_playback)
{
	/* FORCE FIXED 512-byte transport */
	ep_desc->wMaxPacketSize = cpu_to_le16(USB_AUDIO_PACKET_SIZE);
	return 0;
}

/*
 * =========================================================
 * IMPORTANT NOTES
 * =========================================================
 *
 * - DO NOT dynamically scale packet sizes
 * - DO NOT change interval per speed
 * - DO NOT enable async feedback unless needed
 * - This is HARD SYNC mode (Windows stable)
 *
 * =========================================================
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stable UAC2 ALSA-USB Sync Patch");
