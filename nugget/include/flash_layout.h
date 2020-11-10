/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_LAYOUT_H
#define __CROS_EC_FLASH_LAYOUT_H

/*
 * The flash memory is implemented in two halves. The SoC bootrom will look for
 * a first-stage bootloader (aka "RO firmware") at the beginning of each of the
 * two halves and prefer the newer one if both are valid. The chosen bootloader
 * also looks in each half of the flash for a valid application image ("RW
 * firmware"), so we have two possible RW images as well. The RO and RW images
 * are not tightly coupled, so either RO image can choose to boot either RW
 * image. RO images are provided by the SoC team, and can be updated separately
 * from the RW images.
 */

#define CITADEL_FLASH_BASE     0x40000
#define CITADEL_FLASH_SIZE     (512 * 1024)

#define DAUNTLESS_FLASH_BASE   0x80000
#define DAUNTLESS_FLASH_SIZE   (1024 * 1024)

/*
 * Citadel reserves 0x4000 bytes (16K) for its RO firmware. Dauntless can vary,
 * but the RW firmware will follow RO and be aligned on a 16K boundary.
 */
#define FLASH_RW_ALIGNMENT 0x4000

#endif	/* __CROS_EC_FLASH_LAYOUT_H */
