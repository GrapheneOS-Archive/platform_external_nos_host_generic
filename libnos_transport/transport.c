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

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <application.h>

#include "crc16.h"

/* Note: evaluates expressions multiple times */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define VERBOSE_LOG 0
#define DEBUG_LOG 0

#ifdef ANDROID
/* Logging for Android */
#define LOG_TAG "libnos_transport"
#include <sys/types.h>
#include <log/log.h>

#define NLOGE(...) ALOGE(__VA_ARGS__)
#define NLOGV(...) ALOGV(__VA_ARGS__)
#define NLOGD(...) ALOGD(__VA_ARGS__)

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

/* Citadel might take up to 100ms to wake up */
#define RETRY_COUNT 25
#define RETRY_WAIT_TIME_US 5000

/* In case of CRC error, try to retransmit */
#define CRC_RETRY_COUNT 3

struct transport_context {
  const struct nos_device *dev;
  uint8_t app_id;
  uint16_t params;
  const uint8_t *args;
  uint32_t arg_len;
  uint8_t *reply;
  uint32_t *reply_len;
};

/*
 * Read a datagram from the device, correctly handling retries.
 */
static int nos_device_read(const struct nos_device *dev, uint32_t command,
                           void *buf, uint32_t len) {
  int retries = RETRY_COUNT;
  while (retries--) {
    int err = dev->ops.read(dev->ctx, command, buf, len);

    if (err == -EAGAIN) {
      /* Linux driver returns EAGAIN error if Citadel chip is asleep.
       * Give to the chip a little bit of time to awake and retry reading
       * status again. */
      usleep(RETRY_WAIT_TIME_US);
      continue;
    }

    if (err) {
      NLOGE("Failed to read: %s", strerror(-err));
    }
    return -err;
  }

  return ETIMEDOUT;
}

/*
 * Write a datagram to the device, correctly handling retries.
 */
static int nos_device_write(const struct nos_device *dev, uint32_t command,
                            const void *buf, uint32_t len) {
  int retries = RETRY_COUNT;
  while (retries--) {
    int err = dev->ops.write(dev->ctx, command, buf, len);

    if (err == -EAGAIN) {
      /* Linux driver returns EAGAIN error if Citadel chip is asleep.
       * Give to the chip a little bit of time to awake and retry reading
       * status again. */
      usleep(RETRY_WAIT_TIME_US);
      continue;
    }

    if (err) {
      NLOGE("Failed to write: %s", strerror(-err));
    }
    return -err;
  }

  return ETIMEDOUT;
}

#define GET_STATUS_OK              0
#define GET_STATUS_ERROR_IO       -1
#define GET_STATUS_ERROR_PROTOCOL -2

/*
 * Get the status regardless of protocol version. This means some fields might
 * not be set meaningfully so the caller must check the version before accessing
 * them.
 *
 * Returns non-zero on error.
 */
static int get_status(const struct transport_context *ctx,
                      struct transport_status *out) {
  int retries = CRC_RETRY_COUNT;
  while (retries--) {
    /* Get the status from the device */
    union {
      struct transport_legacy_status legacy;
      struct transport_status current;
    } status;
    const uint32_t command = CMD_ID(ctx->app_id) | CMD_IS_READ | CMD_TRANSPORT;
    if (0 != nos_device_read(ctx->dev, command, &status, sizeof(status))) {
      NLOGE("Failed to read device status");
      return GET_STATUS_ERROR_IO;
    }

    /*
     * Identify the legacy protocol. This could have been caused by a bit error
     * but which would result in the wrong status and reply_len being used. If
     * it is the legacy protocol, transform it to V1.
     */
    const bool is_legacy = (status.current.magic != TRANSPORT_STATUS_MAGIC);
    /* TODO: deprecate the legacy protocol when it is safe to do so */
    if (is_legacy) {
      out->version = TRANSPORT_LEGACY;
      out->status = status.legacy.status;
      out->reply_len = status.legacy.reply_len;
      return GET_STATUS_OK;
    }

    /* Check the CRC, if it fails we will retry */
    const uint16_t their_crc = status.current.crc;
    status.current.crc = 0;
    const uint16_t our_crc = crc16(&status, sizeof(status));
    if (their_crc != our_crc) {
      NLOGE("Status CRC mismatch: theirs=%04x ours=%04x", their_crc, our_crc);
      continue;
    }

    /* Check this is a version we recognise */
    if (status.current.version != TRANSPORT_V1) {
      NLOGE("Don't recognise transport version: %d", status.current.version);
      return GET_STATUS_ERROR_PROTOCOL;
    }

    /* It all looks good */
    *out = status.current;
    return GET_STATUS_OK;
  }

  NLOGE("Unable to get valid checksum on status");
  return GET_STATUS_ERROR_PROTOCOL;
}

static int clear_status(const struct transport_context *ctx) {
  const uint32_t command = CMD_ID(ctx->app_id) | CMD_TRANSPORT;
  if (nos_device_write(ctx->dev, command, 0, 0) != 0) {
    NLOGE("Failed to clear device status");
    return -1;
  }
  return 0;
}

/*
 * Ensure that the app is in an idle state ready to handle the transaction.
 */
static uint32_t make_ready(const struct transport_context *ctx) {
  struct transport_status status;
  int ret = get_status(ctx, &status);

  if (ret == GET_STATUS_OK) {
    NLOGD("Inspection status=0x%08x reply_len=%d protocol=%s",
          status.status, status.reply_len,
          status.version == TRANSPORT_LEGACY ? "legacy" : "current");
    /* If it's already idle then we're ready to proceed */
    if (status.status == APP_STATUS_IDLE) {
      return APP_SUCCESS;
    }
    /* Continue to try again after clearing state */
  } else if (ret != GET_STATUS_ERROR_PROTOCOL) {
    NLOGE("Failed to inspect device");
    return APP_ERROR_IO;
  }

  /* Try clearing the status */
  NLOGD("Clearing previous status");
  if (clear_status(ctx) != 0) {
    NLOGD("Failed to force idle status");
    return APP_ERROR_IO;
  }

  /* Check again */
  if (get_status(ctx, &status) != GET_STATUS_OK) {
    NLOGE("Failed to get cleared status");
    return APP_ERROR_IO;
  }
  NLOGD("Cleared status=0x%08x reply_len=%d", status.status, status.reply_len);

  /* It's ignoring us and is still not ready, so it's broken */
  if (status.status != APP_STATUS_IDLE) {
    NLOGE("Device is not responding");
    return APP_ERROR_IO;
  }

  return APP_SUCCESS;
}

/*
 * Split request into datagrams and send command to have app process it.
 */
static uint32_t send_command(const struct transport_context *ctx) {
  const uint8_t *args = ctx->args;
  uint16_t arg_len = ctx->arg_len;

  NLOGV("Send command data (%d bytes)", arg_len);
  uint32_t command = CMD_ID(ctx->app_id) | CMD_IS_DATA | CMD_TRANSPORT;
  /* TODO: don't need to send empty packet in non-legacy protocol */
  do {
    /*
     * We can't send more per datagram than the device can accept. For Citadel
     * using the TPM Wait protocol on SPS, this is a constant. For other buses
     * it may not be, but this is what we support here. Due to peculiarities of
     * Citadel's SPS hardware, our protocol requires that we specify the length
     * of what we're about to send in the params field of each Write.
     */
    const uint16_t ulen = MIN(arg_len, MAX_DEVICE_TRANSFER);
    CMD_SET_PARAM(command, ulen);

    NLOGD("Write command 0x%08x, bytes %d", command, ulen);
    if (nos_device_write(ctx->dev, command, args, ulen) != 0) {
      NLOGE("Failed to send datagram to device");
      return APP_ERROR_IO;
    }

    /* Any further Writes needed to send all the args must set the MORE bit */
    command |= CMD_MORE_TO_COME;
    args += ulen;
    arg_len -= ulen;
  } while (arg_len);

  /* Finally, send the "go" command */
  command = CMD_ID(ctx->app_id) | CMD_PARAM(ctx->params);

  /*
   * The outgoing crc covers:
   *
   *   1. the (16-bit) length of args
   *   2. the args buffer (if any)
   *   3. the (16-bit) reply_len_hint
   *   4. the (32-bit) "go" command
   */
  struct transport_command_info command_info = {
    .version = TRANSPORT_V1,
    .reply_len_hint = *ctx->reply_len,
  };
  arg_len = ctx->arg_len;
  command_info.crc = crc16(&arg_len, sizeof(arg_len));
  command_info.crc = crc16_update(ctx->args, ctx->arg_len, command_info.crc);
  command_info.crc = crc16_update(&command_info.reply_len_hint,
                                  sizeof(command_info.reply_len_hint),
                                  command_info.crc);
  command_info.crc = crc16_update(&command, sizeof(command), command_info.crc);

  /* Tell the app to handle the request while also sending the command_info
   * which will be ignored by the legacy protocol. */
  NLOGD("Write command 0x%08x, crc %04x...", command, command_info.crc);
  if (0 != nos_device_write(ctx->dev, command, &command_info, sizeof(command_info))) {
    NLOGE("Failed to send command datagram to device");
    return APP_ERROR_IO;
  }

  return APP_SUCCESS;
}

/*
 * Keep polling until the app says it is done.
 */
uint32_t poll_until_done(const struct transport_context *ctx,
                         struct transport_status *status) {
  uint32_t poll_count = 0;
  NLOGV("Poll the app status until it's done");
  do {
    if (get_status(ctx, status) != GET_STATUS_OK) {
      return APP_ERROR_IO;
    }
    poll_count++;
    NLOGD("%d:  poll=%d status=0x%08x reply_len=%d", __LINE__,
          poll_count, status->status, status->reply_len);
  } while (!(status->status & APP_STATUS_DONE));

  NLOGV("status=0x%08x reply_len=%d...", status->status, status->reply_len);
  return APP_STATUS_CODE(status->status);
}

/*
 * Reconstruct the reply data from datagram stream.
 */
uint32_t receive_reply(const struct transport_context *ctx,
                       const struct transport_status *status) {
  int retries = CRC_RETRY_COUNT;
  while (retries--) {
    NLOGV("Read the reply data (%d bytes)", status->reply_len);

    uint32_t command = CMD_ID(ctx->app_id) | CMD_IS_READ | CMD_TRANSPORT | CMD_IS_DATA;
    uint8_t *reply = ctx->reply;
    uint16_t left = MIN(*ctx->reply_len, status->reply_len);
    uint16_t got = 0;
    uint16_t crc = 0;
    while (left) {
      /* We can't read more per datagram than the device can send */
      const uint16_t gimme = MIN(left, MAX_DEVICE_TRANSFER);
      NLOGD("Read command=0x%08x, bytes=%d", command, gimme);
      if (nos_device_read(ctx->dev, command, reply, gimme) != 0) {
        NLOGE("Failed to receive datagram from device");
        return APP_ERROR_IO;
      }

      /* Any further Reads should set the MORE bit. This only works if Nugget
       * OS sends back CRCs, but that's the only time we'd retry anyway. */
      command |= CMD_MORE_TO_COME;

      crc = crc16_update(reply, gimme, crc);
      reply += gimme;
      left -= gimme;
      got += gimme;
    }
    /* got it all */
    *ctx->reply_len = got;

    /* Legacy protocol doesn't support CRC so hopefully it's ok */
    if (status->version == TRANSPORT_LEGACY) return APP_SUCCESS;

    if (crc == status->reply_crc) return APP_SUCCESS;
    NLOGE("Reply CRC mismatch: theirs=%04x ours=%04x", status->reply_crc, crc);
  }

  NLOGE("Unable to get valid checksum on reply data");
  return APP_ERROR_IO;
}

/*
 * Driver for the master of the transport protocol.
 */
uint32_t nos_call_application(const struct nos_device *dev,
                              uint8_t app_id, uint16_t params,
                              const uint8_t *args, uint32_t arg_len,
                              uint8_t *reply, uint32_t *reply_len)
{
  uint32_t res;
  const struct transport_context ctx = {
    .dev = dev,
    .app_id = app_id,
    .params = params,
    .args = args,
    .arg_len = arg_len,
    .reply = reply,
    .reply_len = reply_len,
  };

  if ((ctx.arg_len && !ctx.args) ||
      (!ctx.reply_len) ||
      (*ctx.reply_len && !ctx.reply)) {
    NLOGE("Invalid args to %s()", __func__);
    return APP_ERROR_IO;
  }

  NLOGV("Calling app %d with params 0x%04x", app_id, params);

  struct transport_status status;
  int retries = CRC_RETRY_COUNT;
  do {
    /* Wake up and wait for Citadel to be ready */
    res = make_ready(&ctx);
    if (res) return res;

    /* Tell the app what to do */
    res = send_command(&ctx);
    if (res) return res;

    /* Wait until the app has finished */
    res = poll_until_done(&ctx, &status);
    if (res == APP_SUCCESS) break;
    if (res != APP_ERROR_CHECKSUM) return res;
    NLOGD("Request checksum error: %d", retries);
  } while (--retries);
  if (retries == 0) return APP_ERROR_IO;

  /* Get the reply, but only if the app produced data and the caller wants it */
  if (ctx.reply && ctx.reply_len && *ctx.reply_len && status.reply_len) {
    res = receive_reply(&ctx, &status);
    if (res) return res;
  } else {
    *reply_len = 0;
  }

  NLOGV("Clear the reply manually for the next caller");
  /* This should work, but isn't completely fatal if it doesn't because the
   * next call will try again. */
  (void)clear_status(&ctx);

  NLOGV("%s returning 0x%x", __func__, APP_STATUS_CODE(status.status));
  return APP_STATUS_CODE(status.status);
}
