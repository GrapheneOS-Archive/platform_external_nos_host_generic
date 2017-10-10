#ifndef NUGGET_AVB_H
#define NUGGET_AVB_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
  AVB_NUM_SLOTS = 8,
  AVB_METADATA_MAX_SIZE = 2048,
  AVB_DEVICE_DATA_SIZE = (256 / 8),
  AVB_SIGNATURE_SIZE = 256,
};

#ifdef __cplusplus
}
#endif

#endif
