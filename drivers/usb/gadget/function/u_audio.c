/*
 * u_audio.c -- USB gadget ALSA engine (MATCHED STABLE CORE)
 */

#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "u_audio.h"
#include "u_uac2.h"

/* =========================================================
 * STATE STRUCTURES (UNCHANGED ARCHITECTURE)
 * ========================================================= */

struct uac_rtd_params {
	struct snd_pcm_substream *ss;
	ssize_t hw_ptr;

	spinlock_t lock;

	int usb_error_count;
	bool stalled;
};

/* =========================================================
 * ERROR HANDLING (SAFE RECOVERY MODEL)
 * ========================================================= */

static void uac_handle_error(struct uac_rtd_params *p)
{
	p->usb_error_count++;

	if (p->usb_error_count > 32)
		p->stalled = true;

	if (p->usb_error_count < 8)
		p->stalled = false;
}

/* =========================================================
 * ISO CALLBACK (WINDOWS-STABLE VERSION)
 * ========================================================= */

static void u_audio_complete(struct usb_ep *ep,
			     struct usb_request *req)
{
	struct uac_rtd_params *p = req->context;
	struct snd_pcm_substream *sub;
	struct snd_pcm_runtime *rt;
	unsigned long flags;

	if (req->status)
		uac_handle_error(p);

	sub = p->ss;
	if (!sub)
		goto requeue;

	snd_pcm_stream_lock_irqsave(sub, flags);
	rt = sub->runtime;

	if (!rt || !snd_pcm_running(sub)) {
		snd_pcm_stream_unlock_irqrestore(sub, flags);
		goto requeue;
	}

	spin_lock(&p->lock);

	if (p->stalled) {
		p->hw_ptr = 0;
		p->stalled = false;
		p->usb_error_count = 0;
	}

	/* safe bounds */
	if (req->actual > rt->dma_bytes) {
		p->hw_ptr = 0;
		goto unlock;
	}

	/* copy ring buffer */
	unsigned int hp = p->hw_ptr;
	unsigned int rem = rt->dma_bytes - hp;

	if (rem < req->actual) {
		memcpy(req->buf, rt->dma_area + hp, rem);
		memcpy(req->buf + rem, rt->dma_area, req->actual - rem);
	} else {
		memcpy(req->buf, rt->dma_area + hp, req->actual);
	}

	p->hw_ptr = (hp + req->actual) % rt->dma_bytes;

unlock:
	spin_unlock(&p->lock);

	if (p->hw_ptr % snd_pcm_lib_period_bytes(sub) == 0)
		snd_pcm_period_elapsed(sub);

	snd_pcm_stream_unlock_irqrestore(sub, flags);

requeue:
	usb_ep_queue(ep, req, GFP_ATOMIC);
}

/* =========================================================
 * TRIGGER (RESET SAFE STATE)
 * ========================================================= */

static int u_audio_trigger(struct snd_pcm_substream *sub, int cmd)
{
	struct uac_rtd_params *p = sub->runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);

	if (cmd == SNDRV_PCM_TRIGGER_START) {
		p->ss = sub;
		p->hw_ptr = 0;
		p->stalled = false;
		p->usb_error_count = 0;
	}

	if (cmd == SNDRV_PCM_TRIGGER_STOP)
		p->ss = NULL;

	spin_unlock_irqrestore(&p->lock, flags);
	return 0;
}

MODULE_LICENSE("GPL");
