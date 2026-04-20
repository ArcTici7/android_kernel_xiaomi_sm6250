#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "u_audio.h"

#define MAX_BUF (PAGE_SIZE * 16)

/*
 * RULES:
 * - no fake clock logic
 * - no error hysteresis hacks
 * - strict modulo pointer safety
 */

struct uac_rtd_params {
	struct snd_pcm_substream *ss;
	unsigned hw_ptr;
	void *rbuf;
	spinlock_t lock;
	int error_count;
	bool stalled;
};

static void handle_error(struct uac_rtd_params *p)
{
	p->error_count++;

	/* SIMPLE AND STABLE */
	if (p->error_count > 32)
		p->stalled = true;
	else if (p->error_count < 8)
		p->stalled = false;
}

static void u_audio_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uac_rtd_params *p = req->context;
	struct snd_pcm_substream *substream = p->ss;
	struct snd_pcm_runtime *rt;
	unsigned long flags;
	unsigned int ptr;

	if (!substream)
		goto requeue;

	snd_pcm_stream_lock_irqsave(substream, flags);
	rt = substream->runtime;

	if (!rt || !snd_pcm_running(substream)) {
		snd_pcm_stream_unlock_irqrestore(substream, flags);
		goto requeue;
	}

	spin_lock(&p->lock);

	if (p->stalled) {
		spin_unlock(&p->lock);
		snd_pcm_stream_unlock_irqrestore(substream, flags);
		goto requeue;
	}

	ptr = p->hw_ptr;

	/* HARD SAFETY BOUNDARY */
	if (ptr >= rt->dma_bytes)
		ptr = 0;

	if (ptr + req->actual <= rt->dma_bytes) {
		memcpy(req->buf, rt->dma_area + ptr, req->actual);
	} else {
		unsigned split = rt->dma_bytes - ptr;
		memcpy(req->buf, rt->dma_area + ptr, split);
		memcpy(req->buf + split, rt->dma_area, req->actual - split);
	}

	p->hw_ptr = (ptr + req->actual) % rt->dma_bytes;

	spin_unlock(&p->lock);

	if ((p->hw_ptr % snd_pcm_lib_period_bytes(substream)) < req->actual)
		snd_pcm_period_elapsed(substream);

	snd_pcm_stream_unlock_irqrestore(substream, flags);

requeue:
	if (usb_ep_queue(ep, req, GFP_ATOMIC))
		handle_error(p);
}

static int uac_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct uac_rtd_params *p = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		p->ss = substream;
		p->stalled = false;
		p->error_count = 0;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		p->ss = NULL;
		break;
	}

	spin_unlock_irqrestore(&p->lock, flags);
	return 0;
}

MODULE_LICENSE("GPL");
