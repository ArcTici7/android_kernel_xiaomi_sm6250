/*
 * u_audio.c -- USB gadget ALSA sound card (STABILITY PATCHED)
 */

#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "u_audio.h"

#define BUFF_SIZE_MAX (PAGE_SIZE * 16)
#define PRD_SIZE_MAX PAGE_SIZE
#define MIN_PERIODS 4

#define UAC_STABLE_MODE 1

struct uac_req {
	struct uac_rtd_params *pp;
	struct usb_request *req;
};

struct uac_rtd_params {
	struct snd_uac_chip *uac;
	bool ep_enabled;

	struct snd_pcm_substream *ss;

	ssize_t hw_ptr;
	void *rbuf;
	unsigned max_psize;
	struct uac_req *ureq;

	spinlock_t lock;

	int usb_error_count;
	bool stalled;
};

struct snd_uac_chip {
	struct g_audio *audio_dev;

	struct uac_rtd_params p_prm;
	struct uac_rtd_params c_prm;

	struct snd_card *card;
	struct snd_pcm *pcm;

	unsigned int p_interval;
	unsigned int p_residue;
	unsigned int p_pktsize;
	unsigned int p_pktsize_residue;
	unsigned int p_framesize;
};

static void uac_handle_usb_error(struct uac_rtd_params *prm)
{
#if UAC_STABLE_MODE
	prm->usb_error_count++;

	if (prm->usb_error_count > 8)
		prm->stalled = true;
#endif
}

/* ================= ISO CALLBACK ================= */

static void u_audio_iso_complete(struct usb_ep *ep,
				 struct usb_request *req)
{
	struct uac_req *ur = req->context;
	struct uac_rtd_params *prm = ur->pp;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned long flags;

	int status = req->status;

	if (status) {
		uac_handle_usb_error(prm);
		if (status == -ESHUTDOWN)
			return;
	}

	if (!prm->ep_enabled) {
		usb_ep_free_request(ep, req);
		return;
	}

	substream = prm->ss;
	if (!substream)
		goto requeue;

	snd_pcm_stream_lock_irqsave(substream, flags);

	runtime = substream->runtime;
	if (!runtime || !snd_pcm_running(substream)) {
		snd_pcm_stream_unlock_irqrestore(substream, flags);
		goto requeue;
	}

	spin_lock(&prm->lock);

	if (prm->stalled) {
		spin_unlock(&prm->lock);
		snd_pcm_stream_unlock_irqrestore(substream, flags);
		goto requeue;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		req->length = runtime->period_size *
			      runtime->frame_bits / 8;
		req->actual = req->length;
	}

	unsigned int hw_ptr = prm->hw_ptr;
	unsigned int pending = runtime->dma_bytes - hw_ptr;

	if (req->actual > runtime->dma_bytes) {
		spin_unlock(&prm->lock);
		snd_pcm_stream_unlock_irqrestore(substream, flags);
		goto requeue;
	}

	if (pending < req->actual) {
		memcpy(req->buf, runtime->dma_area + hw_ptr, pending);
		memcpy(req->buf + pending, runtime->dma_area,
		       req->actual - pending);
	} else {
		memcpy(req->buf, runtime->dma_area + hw_ptr, req->actual);
	}

	prm->hw_ptr = (hw_ptr + req->actual) % runtime->dma_bytes;

	spin_unlock(&prm->lock);

	if ((prm->hw_ptr % snd_pcm_lib_period_bytes(substream)) < req->actual)
		snd_pcm_period_elapsed(substream);

	snd_pcm_stream_unlock_irqrestore(substream, flags);

requeue:
	if (usb_ep_queue(ep, req, GFP_ATOMIC)) {
		uac_handle_usb_error(prm);
	}
}

/* ================= TRIGGER ================= */

static int uac_pcm_trigger(struct snd_pcm_substream *substream,
			   int cmd)
{
	struct snd_uac_chip *uac = snd_pcm_substream_chip(substream);
	struct uac_rtd_params *prm;
	unsigned long flags;

	prm = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		? &uac->p_prm : &uac->c_prm;

	spin_lock_irqsave(&prm->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		prm->ss = substream;
		prm->stalled = false;
		prm->usb_error_count = 0;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		prm->ss = NULL;
		prm->stalled = false;
		break;
	}

	spin_unlock_irqrestore(&prm->lock, flags);

	return 0;
}
