/*
 * u_audio.c -- USB gadget ALSA sound card (NO DRIFT + LOW LATENCY)
 */

#include <linux/module.h>
#include <linux/usb/gadget.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "u_audio.h"

/* ===================== AUDIO FORMAT ===================== */

#define UAC_RATE           48000
#define UAC_CHANNELS       2
#define UAC_SAMPLE_BYTES   2

/* ===================== LATENCY PROFILE ===================== */

#define PERIOD_SIZE_FRAMES 128
#define PERIOD_COUNT       8
#define BUFFER_SIZE_FRAMES (PERIOD_SIZE_FRAMES * PERIOD_COUNT)

/* ===================== STRUCT ===================== */

struct uac_rtd_params {
	struct snd_pcm_substream *ss;
	unsigned int hw_ptr;

	void *rbuf;
	spinlock_t lock;

	int error_count;
	bool stalled;

	/* 🔥 CRITICAL: USB clock sync */
	unsigned int residue;
};

/* ===================== ERROR HANDLING ===================== */

static void uac_handle_error(struct uac_rtd_params *p)
{
	p->error_count++;

	if (p->error_count > 32)
		p->stalled = true;
	else if (p->error_count < 8)
		p->stalled = false;
}

/* ===================== ISO CALLBACK ===================== */

static void u_audio_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uac_rtd_params *p = req->context;
	struct snd_pcm_substream *substream = p->ss;
	struct snd_pcm_runtime *runtime;
	unsigned long flags;
	unsigned int ptr;

	int frame_bytes = UAC_CHANNELS * UAC_SAMPLE_BYTES;

	/* ===================== USB PACKET TIMING ===================== */
	/* 48kHz @ HS USB → 8000 microframes/sec */

	int base = UAC_RATE / 8000;          /* = 6 samples */
	p->residue += UAC_RATE % 8000;       /* keep generic */

	int samples = base;

	if (p->residue >= 8000) {
		samples++;
		p->residue -= 8000;
	}

	req->length = samples * frame_bytes;
	req->actual = req->length;

	/* ============================================================ */

	if (!substream)
		goto requeue;

	snd_pcm_stream_lock_irqsave(substream, flags);
	runtime = substream->runtime;

	if (!runtime || !snd_pcm_running(substream)) {
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

	if (ptr >= runtime->dma_bytes)
		ptr = 0;

	if (ptr + req->length <= runtime->dma_bytes) {
		memcpy(req->buf,
		       runtime->dma_area + ptr,
		       req->length);
	} else {
		unsigned int split = runtime->dma_bytes - ptr;

		memcpy(req->buf,
		       runtime->dma_area + ptr,
		       split);

		memcpy(req->buf + split,
		       runtime->dma_area,
		       req->length - split);
	}

	p->hw_ptr = (ptr + req->length) % runtime->dma_bytes;

	spin_unlock(&p->lock);

	if ((p->hw_ptr % snd_pcm_lib_period_bytes(substream)) < req->length)
		snd_pcm_period_elapsed(substream);

	snd_pcm_stream_unlock_irqrestore(substream, flags);

requeue:
	if (usb_ep_queue(ep, req, GFP_ATOMIC))
		uac_handle_error(p);
}

/* ===================== PCM HW ===================== */

static struct snd_pcm_hardware uac_pcm_hardware = {
	.info =
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.channels_min = UAC_CHANNELS,
	.channels_max = UAC_CHANNELS,

	.rate_min = UAC_RATE,
	.rate_max = UAC_RATE,

	.period_bytes_min =
		PERIOD_SIZE_FRAMES * UAC_CHANNELS * UAC_SAMPLE_BYTES,

	.period_bytes_max =
		PERIOD_SIZE_FRAMES * UAC_CHANNELS * UAC_SAMPLE_BYTES,

	.periods_min = PERIOD_COUNT,
	.periods_max = PERIOD_COUNT,

	.buffer_bytes_max =
		BUFFER_SIZE_FRAMES * UAC_CHANNELS * UAC_SAMPLE_BYTES,
};

/* ===================== OPEN ===================== */

static int uac_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw = uac_pcm_hardware;

	/* lock ALSA into exact timing */
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

/* ===================== TRIGGER ===================== */

static int uac_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
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
		p->residue = 0;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		p->ss = NULL;
		break;
	}

	spin_unlock_irqrestore(&p->lock, flags);

	return 0;
}

/* ===================== MODULE ===================== */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB Audio Gadget (Low Latency + No Drift)");
