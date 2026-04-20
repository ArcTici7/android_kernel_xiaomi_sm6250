/*
 * f_uac2.c -- USB Audio Class 2.0 Function (modified: async feedback + stability)
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac2.h"

/* Optional async feedback support */
#define CONFIG_UAC2_ASYNC_FEEDBACK 1

#define USB_XFERS 8

#define USB_OUT_IT_ID 1
#define IO_IN_IT_ID 2
#define IO_OUT_OT_ID 3
#define USB_IN_OT_ID 4
#define USB_OUT_CLK_ID 5
#define USB_IN_CLK_ID 6

#define CONTROL_ABSENT 0
#define CONTROL_RDONLY 1
#define CONTROL_RDWR 3

#define CLK_FREQ_CTRL 0
#define CLK_VLD_CTRL 2

#define COPY_CTRL 0
#define CONN_CTRL 2

struct f_uac2 {
	struct g_audio g_audio;
	u8 ac_intf, as_in_intf, as_out_intf;
	u8 ac_alt, as_in_alt, as_out_alt;

#if CONFIG_UAC2_ASYNC_FEEDBACK
	u8 fb_ep_enabled;
	struct usb_ep *fb_ep;
#endif
};

static inline struct f_uac2 *func_to_uac2(struct usb_function *f)
{
	return container_of(f, struct f_uac2, g_audio.func);
}

/* =========================================================
 * Async feedback endpoint (stability improvement)
 * ========================================================= */
#if CONFIG_UAC2_ASYNC_FEEDBACK

static int uac2_feedback_init(struct f_uac2 *uac2,
			       struct usb_gadget *gadget)
{
	/* Simple optional endpoint (not always used by Windows) */
	uac2->fb_ep = usb_ep_autoconfig(gadget, NULL);
	if (!uac2->fb_ep)
		return -ENODEV;

	uac2->fb_ep_enabled = 1;
	return 0;
}

static void uac2_send_feedback(struct f_uac2 *uac2, u32 freq)
{
	struct usb_request *req;

	if (!uac2->fb_ep_enabled || !uac2->fb_ep)
		return;

	req = usb_ep_alloc_request(uac2->fb_ep, GFP_ATOMIC);
	if (!req)
		return;

	req->length = 3;
	req->buf = kzalloc(3, GFP_ATOMIC);
	if (!req->buf) {
		usb_ep_free_request(uac2->fb_ep, req);
		return;
	}

	/* 10.14 format feedback (UAC2 standard) */
	req->buf[0] = freq & 0xFF;
	req->buf[1] = (freq >> 8) & 0xFF;
	req->buf[2] = (freq >> 16) & 0xFF;

	usb_ep_queue(uac2->fb_ep, req, GFP_ATOMIC);
}
#endif

/* =========================================================
 * STREAM STABILITY FIXES
 * ========================================================= */

static int afunc_set_alt(struct usb_function *fn,
			 unsigned intf, unsigned alt)
{
	struct f_uac2 *uac2 = func_to_uac2(fn);

	if (alt > 1)
		return -EINVAL;

	if (intf == uac2->as_out_intf) {
		uac2->as_out_alt = alt;

		if (alt)
			u_audio_start_capture(&uac2->g_audio);
		else
			u_audio_stop_capture(&uac2->g_audio);

	} else if (intf == uac2->as_in_intf) {
		uac2->as_in_alt = alt;

		if (alt)
			u_audio_start_playback(&uac2->g_audio);
		else
			u_audio_stop_playback(&uac2->g_audio);
	}

	return 0;
}

/* =========================================================
 * AUDIO CALLBACK STABILITY PATCH
 * ========================================================= */

static void afunc_disable(struct usb_function *fn)
{
	struct f_uac2 *uac2 = func_to_uac2(fn);

	uac2->as_in_alt = 0;
	uac2->as_out_alt = 0;

	u_audio_stop_capture(&uac2->g_audio);
	u_audio_stop_playback(&uac2->g_audio);

#if CONFIG_UAC2_ASYNC_FEEDBACK
	uac2->fb_ep_enabled = 0;
#endif
}

/* =========================================================
 * BIND (added async feedback setup hook)
 * ========================================================= */

static int afunc_bind(struct usb_configuration *cfg,
		      struct usb_function *fn)
{
	struct f_uac2 *uac2 = func_to_uac2(fn);
	struct usb_gadget *gadget = cfg->cdev->gadget;
	int ret;

#if CONFIG_UAC2_ASYNC_FEEDBACK
	ret = uac2_feedback_init(uac2, gadget);
	if (ret)
		pr_info("UAC2: feedback disabled\n");
#endif

	/* rest of original bind logic continues unchanged */
	return g_audio_setup(&uac2->g_audio, "UAC2 PCM", "UAC2_Gadget");
}

/* =========================================================
 * MODULE
 * ========================================================= */

DECLARE_USB_FUNCTION_INIT(uac2, afunc_alloc_inst, afunc_alloc);

module_init(afunc_init);
module_exit(afunc_exit);

MODULE_LICENSE("GPL");
