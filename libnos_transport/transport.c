/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <nos/transport.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <application.h>

/* Note: evaluates expressions multiple times */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define VERBOSE_LOG 0
#define DEBUG_LOG 0

#ifdef ANDROID
/* Logging for Android */
#include <sys/types.h>
#include <log/log.h>

#define NLOGE(...) ALOGE(__VA_ARGS__)
#define NLOGV(...) do { if (VERBOSE_LOG) { ALOGV(__VA_ARGS__); } } while (0)
#define NLOGD(...) do { if (DEBUG_LOG) { ALOGD(__VA_ARGS__); } } while (0)

extern int usleep (uint32_t usec);

#else
/* Logging for other platforms */
#include <stdio.h>

#define NLOGE(...) do { fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n"); } while (0)
#define NLOGV(...) do { if (VERBOSE_LOG) { \
  printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NLOGD(...) do { if (DEBUG_LOG) { \
  printf(__VA_ARGS__); printf("\n"); } } while (0)

#endif

/* status is non-zero on error */
static int get_status(const struct nos_device *dev,
                      uint8_t app_id, uint32_t *status, uint16_t *ulen)
{
  uint8_t buf[6];
  uint32_t command = CMD_ID(app_id) | CMD_IS_READ | CMD_TRANSPORT;

  if (0 != dev->ops.read(dev->ctx, command, buf, sizeof(buf))) {
    NLOGV("Failed to read device status");
    return -1;
  }

  *status = *(uint32_t *)buf;
  *ulen = *(uint16_t *)(buf + 4);
  return 0;
}

static int clear_status(const struct nos_device *dev, uint8_t app_id)
{
  uint32_t command = CMD_ID(app_id) | CMD_TRANSPORT;

  if (0 != dev->ops.write(dev->ctx, command, 0, 0)) {
    NLOGE("Failed to clear device status");
    return -1;
  }

  return 0;
}


uint32_t nos_call_application(const struct nos_device *dev,
                              uint8_t app_id, uint16_t params,
                              const uint8_t *args, uint32_t arg_len,
                              uint8_t *reply, uint32_t *reply_len)
{
  uint32_t command;
  uint8_t buf[MAX_DEVICE_TRANSFER];
  uint32_t status;
  uint16_t ulen;
  uint32_t poll_count = 0;
  uint16_t retries = 10;

  /* Make sure it's idle */
  while (retries) {
    if (get_status(dev, app_id, &status, &ulen) == 0) {
      break;
    }
    --retries;
    usleep(5000);
  }
  if (!retries) {
    NLOGE("Failed to read device status");
    return APP_ERROR_IO;
  }
  NLOGV("%d: query status 0x%08x  ulen 0x%04x", __LINE__, status, ulen);

  /* It's not idle, but we're the only ones telling it what to do, so it
   * should be. */
  if (status != APP_STATUS_IDLE) {

    /* Try clearing the status */
    NLOGV("clearing previous status");
    if (clear_status(dev, app_id) != 0) {
      return APP_ERROR_IO;
    }

    /* Check again */
    if (get_status(dev, app_id, &status, &ulen) != 0) {
      return APP_ERROR_IO;
    }
    NLOGV("%d: query status 0x%08x  ulen 0x%04x\n",__LINE__,
          status, ulen);

    /* It's ignoring us and is still not ready, so it's broken */
    if (status != APP_STATUS_IDLE) {
      NLOGE("Device is not responding");
      return APP_ERROR_IO;
    }
  }

  /* Send args data */
  command = CMD_ID(app_id) | CMD_TRANSPORT | CMD_IS_DATA;
  do {
    /*
     * We can't send more than the device can take. For
     * Citadel using the TPM Wait protocol on SPS, this is
     * a constant. For other buses, it may not be.
     *
     * For each Write, Citadel requires that we send the length of
     * what we're about to send in the params field.
     */
    ulen = MIN(arg_len, MAX_DEVICE_TRANSFER);
    CMD_SET_PARAM(command, ulen);
    if (args && ulen)
      memcpy(buf, args, ulen);

    NLOGV("Write command 0x%08x, bytes 0x%x\n", command, ulen);

    if (0 != dev->ops.write(dev->ctx, command, buf, ulen)) {
      NLOGE("Failed to send datagram to device");
      return APP_ERROR_IO;
    }

    /* Additional data needs the additional flag set */
    command |= CMD_MORE_TO_COME;

    if (args)
      args += ulen;
    if (arg_len)
      arg_len -= ulen;
  } while (arg_len);

  /* See if we had any errors while sending the args */
  if (get_status(dev, app_id, &status, &ulen) != 0) {
    return APP_ERROR_IO;
  }
  NLOGV("%d: query status 0x%08x  ulen 0x%04x\n", __LINE__, status, ulen);
  if (status & APP_STATUS_DONE)
    /* Yep, problems. It should still be idle. */
    goto reply;

  /* Now tell the app to do whatever */
  command = CMD_ID(app_id) | CMD_PARAM(params);
  NLOGV("Write command 0x%08x\n", command);
  if (0 != dev->ops.write(dev->ctx, command, 0, 0)) {
      NLOGE("Failed to send command datagram to device");
      return APP_ERROR_IO;
  }

  /* Poll the app status until it's done */
  do {
    if (get_status(dev, app_id, &status, &ulen) != 0) {
      return APP_ERROR_IO;
    }
    NLOGD("%d:  poll status 0x%08x  ulen 0x%04x\n", __LINE__,
          status, ulen);
    poll_count++;
  } while (!(status & APP_STATUS_DONE));
  NLOGV("polled %d times, status 0x%08x  ulen 0x%04x\n", poll_count,
        status, ulen);

reply:
  /* Read any result only if there's a place with room to put it */
  if (reply && reply_len && *reply_len) {
    uint16_t left = MIN(*reply_len, ulen);
    uint16_t gimme, got;

    command = CMD_ID(app_id) | CMD_IS_READ |
      CMD_TRANSPORT | CMD_IS_DATA;

    got = 0 ;
    while (left) {

      /*
       * We can't read more than the device can send. For
       * Citadel using the TPM Wait protocol on SPS, this is
       * a constant. For other buses, it may not be.
       */
      gimme = MIN(left, MAX_DEVICE_TRANSFER);
      NLOGV("Read command 0x%08x, bytes 0x%x\n",
            command, gimme);
      if (0 != dev->ops.read(dev->ctx, command, buf, gimme)) {
        NLOGE("Failed to receive datagram from device");
        return APP_ERROR_IO;
      }

      memcpy(reply, buf, gimme);
      reply += gimme;
      left -= gimme;
      got += gimme;
    }
    /* got it all */
    *reply_len = got;
  }

  /* Clear the reply manually for the next caller */
  command = CMD_ID(app_id) | CMD_TRANSPORT;
  if (0 != dev->ops.write(dev->ctx, command, 0, 0)) {
    NLOGE("Failed to clear the reply");
    return APP_ERROR_IO;
  }

  return APP_STATUS_CODE(status);
}
