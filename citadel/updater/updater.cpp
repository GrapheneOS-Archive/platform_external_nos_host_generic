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
  int enable_ro;
  int enable_rw;
  int change_pw;
  uint32_t erase_code;
  /* generic connection options */
  const char *device;
} options;

enum no_short_opts_for_these {
  OPT_DEVICE = 1000,
  OPT_RO,
  OPT_RW,
  OPT_REBOOT,
  OPT_ENABLE_RO,
  OPT_ENABLE_RW,
  OPT_CHANGE_PW,
  OPT_ERASE,
};

const char *short_opts = ":hv";
const struct option long_opts[] = {
  /* name    hasarg *flag val */
  {"version",     0, NULL, 'v'},
  {"ro",          0, NULL, OPT_RO},
  {"rw",          0, NULL, OPT_RW},
  {"reboot",      0, NULL, OPT_REBOOT},
  {"enable_ro",   0, NULL, OPT_ENABLE_RO},
  {"enable_rw",   0, NULL, OPT_ENABLE_RW},
  {"change_pw",   0, NULL, OPT_CHANGE_PW},
  {"erase",       1, NULL, OPT_ERASE},
  {"device",      1, NULL, OPT_DEVICE},
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
    "\n"
    "      --enable_ro   Mark new RO image as good\n"
    "      --enable_rw   Mark new RW image as good\n"
    "\n"
    "      --change_pw   Change update password\n"
    "\n\n"
    "      --erase=CODE  Erase all user secrets and reboot.\n"
    "                    This skips all other actions.\n"
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

uint32_t compute_digest(void *ptr, size_t len)
{
  SHA_CTX ctx;
  uint8_t digest[SHA_DIGEST_LENGTH];
  uint32_t retval;

  SHA1_Init(&ctx);
  SHA1_Update(&ctx, ptr, len);
  SHA1_Final(digest, &ctx);

  memcpy(&retval, digest, sizeof(retval));
  return retval;
}

uint32_t compute_fb_digest(struct nugget_app_flash_block *blk)
{
  uint8_t *start_here = ((uint8_t *)blk) +
    offsetof(struct nugget_app_flash_block, offset);
  size_t size_to_hash = sizeof(*blk) -
    offsetof(struct nugget_app_flash_block, offset);

  return compute_digest(start_here, size_to_hash);
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
    fb->block_digest = compute_fb_digest(fb);

    printf("writing 0x%05x / 0x%05x",
           CHIP_FLASH_BASE + offset, CHIP_FLASH_BASE + stop);
    do {
      rv = app.Call(NUGGET_PARAM_FLASH_BLOCK, data, nullptr);
      if (rv == NUGGET_ERROR_RETRY)
        printf(" retrying");
    } while (rv == NUGGET_ERROR_RETRY && retries--);
    if (rv) {
      if (rv == NUGGET_ERROR_LOCKED)
        printf(" locked\n");
      else
        printf(" fail %d\n", rv);
      break;
    }
    printf(" ok\n");
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

static uint32_t do_change_pw(AppClient &app,
                             const char *old_pw, const char *new_pw)
{
  std::vector<uint8_t> data(sizeof(struct nugget_app_change_update_password));
  struct nugget_app_change_update_password *s =
    (struct nugget_app_change_update_password*)data.data();


  memset(s, 0xff, sizeof(*s));
  if (old_pw && *old_pw) {
    int len = strlen(old_pw);
    memcpy(&s->old_password.password, old_pw, len);
    s->old_password.digest =
      compute_digest(&s->old_password.password,
                     sizeof(s->old_password.password));
  }

  if (new_pw && *new_pw) {
    int len = strlen(new_pw);
    memcpy(&s->new_password.password, new_pw, len);
    s->new_password.digest =
      compute_digest(&s->new_password.password,
                     sizeof(s->new_password.password));
  }

  uint32_t rv = app.Call(NUGGET_PARAM_CHANGE_UPDATE_PASSWORD, data, nullptr);

  if (is_app_success(rv))
    printf("Password changed\n");

  return rv;
}

static uint32_t do_enable(AppClient &app, const char *pw)
{

  std::vector<uint8_t> data(sizeof(struct nugget_app_enable_update));
  struct nugget_app_enable_update *s =
    (struct nugget_app_enable_update*)data.data();

  memset(&s->password, 0xff, sizeof(s->password));
  if (pw && *pw) {
    int len = strlen(pw);
    memcpy(&s->password.password, pw, len);
    s->password.digest =
      compute_digest(&s->password.password,
                     sizeof(s->password.password));
  }
  s->which_headers = options.enable_ro ? NUGGET_ENABLE_HEADER_RO : 0;
  s->which_headers |= options.enable_rw ? NUGGET_ENABLE_HEADER_RW : 0;

  uint32_t rv = app.Call(NUGGET_PARAM_ENABLE_UPDATE, data, nullptr);

  if (is_app_success(rv))
    printf("Update enabled\n");

  return rv;
}

static uint32_t do_erase(AppClient &app)
{
  std::vector<uint8_t> data(sizeof(uint32_t));
  memcpy(data.data(), &options.erase_code, data.size());

  uint32_t rv = app.Call(NUGGET_PARAM_NUKE_FROM_ORBIT, data, nullptr);

  if (is_app_success(rv))
    printf("Citadel erase and reboot requested\n");

  return rv;
}

std::unique_ptr<NuggetClientInterface> select_client()
{
#ifdef ANDROID
  return std::unique_ptr<NuggetClientInterface>(new CitadeldProxyClient());
#else
  return std::unique_ptr<NuggetClientInterface>(
      new NuggetClient(options.device ? options.device : ""));
#endif
}

int execute_commands(const std::vector<uint8_t> &image,
                     const char *old_passwd, const char *passwd)
{
  auto client = select_client();
  client->Open();
  if (!client->IsOpen()) {
    Error("Unable to connect");
    return 1;
  }
  AppClient app(*client, APP_ID_NUGGET);

  /* Try all requested actions in reasonable order, bail out on error */

  if (options.erase_code) {                     /* zero doesn't count */
    /* whether we succeed or not, only do this */
    return do_erase(app);
  }

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

  if (options.change_pw &&
      do_change_pw(app, old_passwd, passwd) != APP_SUCCESS)
    return 5;

  if ((options.enable_ro || options.enable_rw) &&
      do_enable(app, passwd) != APP_SUCCESS)
    return 6;

  if (options.reboot &&
      do_reboot(app) != APP_SUCCESS) {
    return 7;
  }

  return 0;
}

} // namespace

int main(int argc, char *argv[])
{
  int i;
  int idx = 0;
  char *this_prog;
  char *old_passwd = 0;
  char *passwd = 0;
  std::vector<uint8_t> image;
  int got_action = 0;
  char *e = 0;

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
    case OPT_ENABLE_RO:
      options.enable_ro = 1;
      got_action = 1;
      break;
    case OPT_ENABLE_RW:
      options.enable_rw = 1;
      got_action = 1;
      break;
    case OPT_CHANGE_PW:
      options.change_pw = 1;
      got_action = 1;
      break;
    case OPT_ERASE:
      options.erase_code = (uint32_t)strtoul(optarg, &e, 0);
      if (!*optarg || (e && *e)) {
        Error("Invalid argument: \"%s\"\n", optarg);
        errorcnt++;
      }
      got_action = 1;
      break;

      /* generic options below */
    case OPT_DEVICE:
      options.device = optarg;
      break;
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
      if (errorcnt)
        goto out;
    } else {
      Error("An image file is required with --ro and --rw");
      goto out;
    }
  }

  if (options.change_pw) {
    /* one arg provided, maybe the old password isn't set */
    if (optind < argc) {
      passwd = argv[optind++];
    } else {
      Error("Need a new password at least."
            " Use '' to clear it.");
      goto out;
    }
    /* two args provided, use both old & new passwords */
    if (optind < argc) {
      old_passwd = passwd;
      passwd = argv[optind++];
    }
  }

  if ((options.enable_ro || options.enable_rw) && !passwd) {
    if (optind < argc)
      passwd = argv[optind++];
    else {
      Error("Need a password to enable images. Use '' if none.");
      goto out;
    }
  }

  /* Okay, let's do it! */
  (void) execute_commands(image, old_passwd, passwd);
  /* This is the last action, so fall through either way */

out:
  return !!errorcnt;
}
