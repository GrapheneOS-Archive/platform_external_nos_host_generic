/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_INCLUDE_APP_NUGGET_H
#define __CROS_EC_INCLUDE_APP_NUGGET_H
#include "application.h"
#include "config.h"

/****************************************************************************/
/*
 * APP_ID_NUGGET uses the Transport API:
 *
 *   uint32_t call_application(uint8_t app_id, uint16_t app_param,
 *                             uint8_t *args, uint16_t arg_len,
 *                             uint8_t *reply, uint16_t *reply_len);
 *
 * Refer to application.h for details.
 */
/****************************************************************************/

/* App-specific errors */
enum {
	NUGGET_ERROR_LOCKED = APP_SPECIFIC_ERROR,
	NUGGET_ERROR_RETRY,
};

/****************************************************************************/
/* Application functions */

#define NUGGET_PARAM_VERSION 0x0000
/*
 * Return the current build string
 *
 * @param args         <none>
 * @param arg_len      0
 * @param reply        Null-terminated ASCII string
 * @param reply_len    Max length to return
 *
 * @errors             APP_ERROR_TOO_MUCH
 */

/****************************************************************************/
/* Firmware upgrade stuff */

struct nugget_app_flash_block {
	uint32_t block_digest;	    /* first 4 bytes of sha1 of the rest */
	uint32_t offset;	    /* from start of flash */
	uint8_t payload[CHIP_FLASH_BANK_SIZE];	/* data to write */
} __packed;

#define NUGGET_PARAM_FLASH_BLOCK 0x0001
/*
 * Erase and write a single flash block.
 *
 * @param args         struct nugget_app_flash_block
 * @param arg_len      sizeof(struct nugget_app_flash_block)
 * @param reply        <none>
 * @param reply_len    0
 *
 * @errors             NUGGET_ERROR_LOCKED, NUGGET_ERROR_RETRY
 */

#define NUGGET_PARAM_REBOOT 0x0002
/*
 * Reboot Citadel
 *
 * @param args         uint8_t hard        0 = soft reboot, 1 = hard reboot
 * @param arg_len      sizeof(uint8_t)
 * @param reply        <none>
 * @param reply_len    0
 */

/****************************************************************************/
/* These are bringup / debug functions only.
 *
 * TODO(b/65067435): Remove all of these.
 */

#define NUGGET_PARAM_REVERSE 0xbeef
/*
 * Reverse a sequence of bytes, just to have something to demonstrate.
 *
 * @param args         any arbitrary bytes
 * @param arg_len      any arbitrary length, within reason
 * @param reply        input bytes, in reverse order
 * @param reply_len    same as arg_len
 *
 * @errors             APP_ERROR_TOO_MUCH
 */

#define NUGGET_PARAM_READ32 0xF000
/*
 * Read a 32-bit value from memory.
 *
 * DANGER, WILL ROBINSON! DANGER! There is NO sanity checking on this AT ALL.
 * Read the wrong address, and Bad Things(tm) WILL happen.
 *
 * @param args         uint32_t address
 * @param arg_len      sizeof(uint32_t)
 * @param reply        uint32_t value
 * @param reply_len    sizeof(uint32_t)
 */

struct nugget_app_write32 {
	uint32_t address;
	uint32_t value;
} __packed;

#define NUGGET_PARAM_WRITE32 0xF001
/*
 * Write a 32-bit value to memory
 *
 * DANGER, WILL ROBINSON! DANGER! There is NO sanity checking on this AT ALL.
 * Write the wrong values to the wrong address, and Bad Things(tm) WILL happen.
 *
 * @param args         struct nugget_app_write32
 * @param arg_len      sizeof(struct nugget_app_write32)
 * @param reply        <none>
 * @param reply_len    0
 */

#endif	/* __CROS_EC_INCLUDE_APP_NUGGET_H */
