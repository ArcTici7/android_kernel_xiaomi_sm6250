/*
 * f_uac2.c -- USB Audio Class 2.0 Function (Low-Latency Profile)
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac2.h"

#define USB_XFERS 4 /* reduced queue depth for latency */

#define USB_OUT_IT_ID 1
#define IO_IN_IT_ID 2
#define IO_OUT_OT_ID 3
#define USB_IN_OT_ID 4
#define USB_OUT_CLK_ID 5
#define USB_IN_CLK_ID 6

#define CLK_FREQ_CTRL 0
#define CLK_VLD_CTRL 2

#define COPY_CTRL 0

struct f_uac2 {
	struct g_audio g_audio;
	u8 ac_intf, as_in_intf, as_out_intf;
	u8 ac_alt, as_in_alt, as_out_alt;
};

/* ---------------- CLOCK SOURCES (WINDOWS SAFE) ----------------
 * Windows UAC2 stack is strict:
 * - must expose valid clock
 * - must not lie about validity
 */

static struct uac_clock_source_descriptor in_clk_src_desc = {
	.bLength = sizeof(in_clk_src_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,
	.bClockID = USB_IN_CLK_ID,
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_FIXED,
	.bmControls = (1 << CLK_FREQ_CTRL) | (1 << CLK_VLD_CTRL),
	.bAssocTerminal = 0,
};

static struct uac_clock_source_descriptor out_clk_src_desc = {
	.bLength = sizeof(out_clk_src_desc),
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,
	.bClockID = USB_OUT_CLK_ID,
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_FIXED,
	.bmControls = (1 << CLK_FREQ_CTRL) | (1 << CLK_VLD_CTRL),
	.bAssocTerminal = 0,
};

/* ---------------- ULTRA LOW LATENCY ENDPOINT PROFILE ----------------
 * IMPORTANT:
 * - bInterval = 1 (1 microframe @ HS = lowest polling interval)
 * - avoid large bursts
 * - Windows will still buffer but this reduces device-side latency
 */

static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,
	.bInterval = 1,
};

static struct usb_endpoint_descriptor fs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,
	.bInterval = 1,
};

/* ---------------- CONTROL FIX (WINDOWS COMPATIBILITY) ---------------- */

static int in_rq_cur(struct usb_function *fn,
		     const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac2_opts *opts = g_audio_to_uac2_opts(agdev);

	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);

	u8 entity_id = (w_index >> 8) & 0xff;
	u8 cs = w_value >> 8;

	if (cs == UAC2_CS_CONTROL_SAM_FREQ) {
		__le32 v;

		if (entity_id == USB_IN_CLK_ID)
			v = cpu_to_le32(opts->p_srate);
		else if (entity_id == USB_OUT_CLK_ID)
			v = cpu_to_le32(opts->c_srate);
		else
			return -EOPNOTSUPP;

		memcpy(req->buf, &v, min_t(unsigned, w_length, sizeof(v)));
		return sizeof(v);
	}

	if (cs == UAC2_CS_CONTROL_CLOCK_VALID) {
		u8 ok = 1;
		memcpy(req->buf, &ok, 1);
		return 1;
	}

	return -EOPNOTSUPP;
}

/* ---------------- LATENCY NOTES (IMPORTANT REALITY) ----------------
 * This file now runs at device-side minimum buffering.
 *
 * REAL LIMITS:
 * - Windows audio stack: ~10–50ms baseline buffering
 * - ALSA/PulseAudio: adds additional buffering
 * - USB host controller: may coalesce transfers
 *
 * You cannot force <30ms purely here.
 */
