/*
 * u_audio.c -- USB Gadget ALSA Audio (ZERO-COPY DMA PIPELINE)
 *
 * Design goals:
 *  - NO memcpy in ISO data path
 *  - ALSA DMA buffer == USB transfer source
 *  - fixed 48kHz / 144-frame (3ms) cadence
 *  - deterministic ring buffer progression
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb/gadget.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#define RATE            48000
#define CHANNELS        2
#define SAMPLE_BYTES    2

/* 3ms USB-aligned quantum */
#define PERIOD_FRAMES   144
#define PERIOD_BYTES    (PERIOD_FRAMES * CHANNELS * SAMPLE_BYTES)

#define PERIODS         10
#define BUFFER_FRAMES   (PERIOD_FRAMES * PERIODS)
#define BUFFER_BYTES    (BUFFER_FRAMES * CHANNELS * SAMPLE_BYTES)

/* ===================== STATE ===================== */

struct uac_rtd {
	struct snd_pcm_substream *ss;
	struct snd_pcm_runtime *runtime;

	unsigned int hw_ptr;
	spinlock_t lock;

	bool running;
};

/* ===================== PCM HARDWARE ===================== */

static struct snd_pcm_hardware uac_hw = {
	.info =
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.channels_min = CHANNELS,
	.channels_max = CHANNELS,

	.rate_min = RATE,
	.rate_max = RATE,

	.period_bytes_min = PERIOD_BYTES,
	.period_bytes_max = PERIOD_BYTES,

	.periods_min = PERIODS,
	.periods_max = PERIODS,

	.buffer_bytes_max = BUFFER_BYTES,
};

/* ===================== PCM OPEN ===================== */

static int uac_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *rt = substream->runtime;

	rt->hw = uac_hw;

	/* lock deterministic period sizing */
	snd_pcm_hw_constraint_minmax(
		rt,
		SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
		PERIOD_FRAMES,
		PERIOD_FRAMES
	);

	snd_pcm_hw_constraint_minmax(
		rt,
		SNDRV_PCM_HW_PARAM_PERIODS,
		PERIODS,
		PERIODS
	);

	/*
	 * ZERO COPY REQUIREMENT:
	 * ALSA DMA buffer is directly used by USB
	 */
	snd_pcm_lib_preallocate_pages_for_all(
		substream,
		SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL),
		BUFFER_BYTES,
		BUFFER_BYTES
	);

	return 0;
}

/* ===================== TRIGGER ===================== */

static int uac_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct uac_rtd *rtd = substream->private_data;
	unsigned long flags;

	spin_lock_irqsave(&rtd->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		rtd->ss = substream;
		rtd->runtime = substream->runtime;
		rtd->hw_ptr = 0;
		rtd->running = true;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		rtd->running = false;
		rtd->ss = NULL;
		rtd->runtime = NULL;
		break;
	}

	spin_unlock_irqrestore(&rtd->lock, flags);
	return 0;
}

/* ===================== USB ISO CALLBACK ===================== */

static void uac_iso_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uac_rtd *rtd = req->context;
	struct snd_pcm_runtime *rt;
	struct snd_pcm_substream *ss;
	unsigned long flags;
	unsigned int ptr;

	if (!rtd || !rtd->ss)
		goto requeue;

	ss = rtd->ss;
	rt = rtd->runtime;

	if (!rt || !rtd->running)
		goto requeue;

	snd_pcm_stream_lock_irqsave(ss, flags);
	spin_lock(&rtd->lock);

	ptr = rtd->hw_ptr;

	if (ptr >= BUFFER_BYTES)
		ptr = 0;

	/*
	 * 🔥 ZERO COPY CORE:
	 * USB DMA reads directly from ALSA DMA buffer
	 */
	req->buf = rt->dma_area + ptr;
	req->length = PERIOD_BYTES;

	rtd->hw_ptr = ptr + PERIOD_BYTES;

	/* notify ALSA every period */
	if ((rtd->hw_ptr % PERIOD_BYTES) == 0)
		snd_pcm_period_elapsed(ss);

	spin_unlock(&rtd->lock);
	snd_pcm_stream_unlock_irqrestore(ss, flags);

requeue:
	usb_ep_queue(ep, req, GFP_ATOMIC);
}

/* ===================== MODULE ===================== */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UAC2 ZERO-COPY USB Audio Gadget (DMA Direct Path)");
