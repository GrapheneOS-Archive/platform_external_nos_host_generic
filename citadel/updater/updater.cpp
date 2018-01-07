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

#include <memory>
#include <vector>

#include <getopt.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* From Nugget OS */
#include <application.h>
#include <app_nugget.h>
#include <flash_layout.h>
#include <signed_header.h>

#include <nos/AppClient.h>
#include <nos/NuggetClient.h>
#ifdef ANDROID
#include <nos/CitadeldProxyClient.h>
#endif

namespace {

using nos::AppClient;
using nos::NuggetClient;
using nos::NuggetClientInterface;
#ifdef ANDROID
using nos::CitadeldProxyClient;
#endif

/* Global options */
struct options_s {
  /* actions to take */
  int version;
  int ro;
  int rw;
  int reboot;
  /* generic connection options */
  const char *device;
#ifdef ANDROID
  int citadeld;
#endif
} options;

enum no_short_opts_for_these {
  OPT_DEVICE = 1000,
  OPT_RO,
  OPT_RW,
  OPT_REBOOT,
#ifdef ANDROID
  OPT_CITADELD
#endif
};

const char *short_opts = ":hv";
const struct option long_opts[] = {
  /* name    hasarg *flag val */
  {"version",     0, NULL, 'v'},
  {"ro",          0, NULL, OPT_RO},
  {"rw",          0, NULL, OPT_RW},
  {"reboot",      0, NULL, OPT_REBOOT},
  {"device",      1, NULL, OPT_DEVICE},
#ifdef ANDROID
  {"citadeld",    0, NULL, OPT_CITADELD},
#endif
  {"help",        0, NULL, 'h'},
  {NULL, 0, NULL, 0},
};

void usage(const char *progname)
{
  fprintf(stderr, "\n");
  fprintf(stderr,
    "Usage: %s [actions] [image.bin]\n"
    "\n"
    "Citadel firmware boots in two stages. The first stage\n"
    "bootloader (aka \"RO\") is provided by the SOC hardware team\n"
    "and seldom changes. The application image (\"RW\") is invoked\n"
    "by the RO image. There are two copies (A/B) of each stage,\n"
    "so that the active copy can be protected while the unused\n"
    "copy may be updated. At boot, the newer (valid) copy of each\n"
    "stage is selected.\n"
    "\n"
    "The Citadel image file is the same size of the internal\n"
    "flash, and contains all four firmware components (RO_A,\n"
    "RW_A, RO_B, RW_B) located at the correct offsets. Only the\n"
    "inactive copy (A/B) of each stage (RO/RW) can be modified.\n"
    "The tool will update the correct copies automatically.\n"
    "\n"
    "You must specify the actions to perform. With no options,\n"
    "this help message is displayed.\n"
    "\n"
    "Actions:\n"
    "\n"
    "  -v, --version     Display the Citadel version info\n"
    "      --rw          Update RW firmware from the image file\n"
    "      --ro          Update RO firmware from the image file\n"
    "      --reboot      Tell Citadel to reboot\n"
#ifdef ANDROID
    "\n"
    "Android options:\n"
    "\n"
    "      --citadeld    Communicate with Citadel via citadeld\n"
#endif
    "\n",
    progname);
}

/****************************************************************************/
/* Handy stuff */

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

int errorcnt;
void Error(const char *format, ...)
{
  va_list ap;

        va_start(ap, format);
        fprintf(stderr, "ERROR: ");
        vfprintf(stderr, format, ap);
        fprintf(stderr, "\n");
        va_end(ap);

  errorcnt++;
}

/* Return true on APP_SUCESS, display error message if it's not */
int is_app_success(uint32_t retval)
{
  if (retval == APP_SUCCESS)
    return 1;

  errorcnt++;

  fprintf(stderr, "Error code 0x%x: ", retval);
  switch (retval) {
  case APP_ERROR_BOGUS_ARGS:
    fprintf(stderr, "bogus args");
    break;
  case APP_ERROR_INTERNAL:
    fprintf(stderr, "app is being stupid");
    break;
  case APP_ERROR_TOO_MUCH:
    fprintf(stderr, "caller sent too much data");
    break;
  default:
    if (retval >= APP_SPECIFIC_ERROR &&
       retval < APP_LINE_NUMBER_BASE) {
      fprintf(stderr, "app-specific error #%d",
        retval - APP_SPECIFIC_ERROR);
    } else if (retval >= APP_LINE_NUMBER_BASE) {
      fprintf(stderr, "error at line %d",
        retval - APP_LINE_NUMBER_BASE);
    } else {
      fprintf(stderr, "unknown)");
    }
  }
  fprintf(stderr, "\n");

  return 0;
}

/****************************************************************************/

std::vector<uint8_t> read_image_from_file(const char *name)
{
  FILE *fp;
  struct stat st;

  fp = fopen(name, "rb");
  if (!fp) {
    perror("fopen");
    Error("Can't open file %s", name);
    return {};
  }

  if (fstat(fileno(fp), &st)) {
    perror("fstat");
    Error("Can't fstat file %s", name);
    fclose(fp);
    return {};
  }

  if (st.st_size != CHIP_FLASH_SIZE) {
    Error("The firmware image must be exactly %d bytes",
          CHIP_FLASH_SIZE);
    fclose(fp);
    return {};
  }

  std::vector<uint8_t> buf(st.st_size);
  if (1 != fread(buf.data(), st.st_size, 1, fp)) {
    perror("fread");
    Error("Can't read %zd bytes", st.st_size);
    fclose(fp);
    return {};
  }

  fclose(fp);
  buf.resize(st.st_size);

  return buf;
}

uint32_t compute_digest(struct nugget_app_flash_block *blk)
{
  uint8_t *start_here = ((uint8_t *)blk) +
    offsetof(struct nugget_app_flash_block, offset);
  size_t size_to_hash = sizeof(*blk) -
    offsetof(struct nugget_app_flash_block, offset);
  SHA_CTX ctx;
  uint8_t digest[SHA_DIGEST_LENGTH];
  uint32_t retval;

  SHA1_Init(&ctx);
  SHA1_Update(&ctx, start_here, size_to_hash);
  SHA1_Final(digest, &ctx);

  memcpy(&retval, digest, sizeof(retval));
  return retval;
}

uint32_t try_update(AppClient &app, const std::vector<uint8_t> &image,
        uint32_t offset, uint32_t imagesize)
{
  uint32_t stop = offset + imagesize;
  uint32_t rv;

  printf("Updating image from 0x%05x to 0x%05x, size 0x%05x\n",
         CHIP_FLASH_BASE + offset, CHIP_FLASH_BASE + stop, imagesize);

  for (; offset < stop; offset += CHIP_FLASH_BANK_SIZE) {
    int retries = 3;
    std::vector<uint8_t> data(sizeof(struct nugget_app_flash_block));
    struct nugget_app_flash_block *fb =
      (struct nugget_app_flash_block*)data.data();

    fb->offset = offset;
    memcpy(fb->payload, image.data() + offset, CHIP_FLASH_BANK_SIZE);
    fb->block_digest = compute_digest(fb);

    printf("writing 0x%05x / 0x%05x",
           CHIP_FLASH_BASE + offset, CHIP_FLASH_BASE + stop);
    do {
      rv = app.Call(NUGGET_PARAM_FLASH_BLOCK, data, nullptr);
      if (rv == NUGGET_ERROR_RETRY)
        printf(" retrying");
    } while (rv == NUGGET_ERROR_RETRY && retries--);
    printf(" %s\n", rv ? "fail" : "ok");
    if (rv)
      break;
  }

  return rv;
}


uint32_t do_update(AppClient &app, const std::vector<uint8_t> &image,
       uint32_t offset_A, uint32_t offset_B)
{
  struct SignedHeader *hdr;
  uint32_t rv_A, rv_B;

  /* Try image A first */
  hdr = (struct SignedHeader *)(image.data() + offset_A);
  rv_A = try_update(app, image, offset_A, hdr->image_size);

  /* If that worked, we're done */
  if (rv_A == APP_SUCCESS) {
    return rv_A;
  }

  /* Else try image B */
  hdr = (struct SignedHeader *)(image.data() + offset_B);
  rv_B = try_update(app, image, offset_B, hdr->image_size);

  return rv_B;
}

uint32_t do_version(AppClient &app)
{
  uint32_t retval;
  std::vector<uint8_t> buffer;
  buffer.reserve(512);

  retval = app.Call(NUGGET_PARAM_VERSION, buffer, &buffer);

  if (is_app_success(retval)) {
    printf("%.*s\n", (int) buffer.size(), buffer.data());
  }

  return retval;
}

uint32_t do_reboot(AppClient &app)
{
  uint32_t retval;
  std::vector<uint8_t> data = {0};

  retval = app.Call(NUGGET_PARAM_REBOOT, data, nullptr);

  if (is_app_success(retval)) {
    printf("Citadel reboot requested\n");
  }

  return retval;
}

std::unique_ptr<NuggetClientInterface> select_client()
{
#ifdef ANDROID
  if (options.citadeld) {
    return std::unique_ptr<NuggetClientInterface>(
        new CitadeldProxyClient());
  }
#endif
  /* Default to a direct client */
  return std::unique_ptr<NuggetClientInterface>(
      new NuggetClient(options.device ? options.device : ""));
}

int update_to_image(const std::vector<uint8_t> &image)
{
  auto client = select_client();
  client->Open();
  if (!client->IsOpen()) {
    Error("Unable to connect");
    return 1;
  }
  AppClient app(*client, APP_ID_NUGGET);

  /* Try all requested actions in reasonable order, bail out on error */

  if (options.version &&
      do_version(app) != APP_SUCCESS) {
    return 2;
  }

  if (options.rw &&
      do_update(app, image,
          CHIP_RW_A_MEM_OFF, CHIP_RW_B_MEM_OFF) != APP_SUCCESS) {
    return 3;
  }

  if (options.ro &&
      do_update(app, image,
          CHIP_RO_A_MEM_OFF, CHIP_RO_B_MEM_OFF) != APP_SUCCESS) {
    return 4;
  }

  if (options.reboot &&
      do_reboot(app) != APP_SUCCESS) {
    return 5;
  }
  return 0;
}

} // namespace

int main(int argc, char *argv[])
{
  int i;
  int idx = 0;
  char *this_prog;
  std::vector<uint8_t> image;
  int got_action = 0;

  this_prog= strrchr(argv[0], '/');
        if (this_prog) {
    this_prog++;
  } else {
    this_prog = argv[0];
  }

        opterr = 0;        /* quiet, you */
  while ((i = getopt_long(argc, argv,
        short_opts, long_opts, &idx)) != -1) {
    switch (i) {
      /* program-specific options */
    case 'v':
      options.version = 1;
      got_action = 1;
      break;
    case OPT_RO:
      options.ro = 1;
      got_action = 1;
      break;
    case OPT_RW:
      options.rw = 1;
      got_action = 1;
      break;
    case OPT_REBOOT:
      options.reboot = 1;
      got_action = 1;
      break;

      /* generic options below */
    case OPT_DEVICE:
      options.device = optarg;
      break;
#ifdef ANDROID
    case OPT_CITADELD:
      options.citadeld = 1;
      break;
#endif
    case 'h':
      usage(this_prog);
      return 0;
    case 0:
      break;
    case '?':
      if (optopt)
        Error("Unrecognized options: -%c", optopt);
      else
        Error("Unrecognized options: %s",
              argv[optind - 1]);
      usage(this_prog);
      break;
    case ':':
      Error("Missing argument to %s", argv[optind - 1]);
      break;
    default:
      Error("Internal error at %s:%d", __FILE__, __LINE__);
      exit(1);
    }
        }

  if (errorcnt) {
    goto out;
  }

  if (!got_action) {
    usage(this_prog);
    goto out;
  }

  if (options.ro || options.rw) {
    if (optind < argc) {
      /* Sets errorcnt on failure */
      image = read_image_from_file(argv[optind]);
    } else {
      Error("An image file is required with --ro and --rw");
    }
  }

  if (errorcnt)
    goto out;

  /* Okay, let's do something */
  (void) update_to_image(image);
  /* This is the last action, so fall through either way */

out:
  return !!errorcnt;
}
