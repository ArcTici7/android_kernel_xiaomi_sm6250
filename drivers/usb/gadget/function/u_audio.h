#ifndef __U_UAC2_H
#define __U_UAC2_H

#include <linux/usb/composite.h>

/* =========================================================
 * UAC2 TOPOLOGY IDS (MUST MATCH f_uac2.c)
 * ========================================================= */

#define UAC2_OUT_IT_ID      1
#define UAC2_IN_IT_ID       2
#define UAC2_OUT_OT_ID      3
#define UAC2_IN_OT_ID       4

#define UAC2_CLK_OUT_ID     5
#define UAC2_CLK_IN_ID      6

/* =========================================================
 * AUDIO FORMAT (FIXED FOR WINDOWS COMPATIBILITY)
 * ========================================================= */

#define UAC2_FORMAT_RATE     48000
#define UAC2_CHANNELS        2
#define UAC2_SAMPLE_SIZE     2   /* 16-bit PCM */

/* =========================================================
 * PACKET BEHAVIOR (CRITICAL CONSISTENCY)
 * ========================================================= */

#define UAC2_INTERVAL        1
#define UAC2_MAX_PACKET_FS   200
#define UAC2_MAX_PACKET_HS   1024

/* =========================================================
 * SYNC MODE (MUST MATCH ENDPOINTS)
 * ========================================================= */

#define UAC2_SYNC_ASYNC_OUT   1
#define UAC2_SYNC_ADAPTIVE_IN 1

/* =========================================================
 * BUFFERING (LOW LATENCY SAFE)
 * ========================================================= */

#define UAC2_MIN_PERIODS      4
#define UAC2_MAX_PERIODS      8

#endif
