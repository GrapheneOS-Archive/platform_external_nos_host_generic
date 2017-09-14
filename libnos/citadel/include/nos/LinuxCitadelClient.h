#ifndef NOS_LINUX_CITADEL_CLIENT_H
#define NOS_LINUX_CITADEL_CLIENT_H

extern "C" {
#include "core/citadel/config_chip.h"  // Included for CHIP_FLASH_BANK_SIZE.
}
#include <application.h>
#include <app_nugget.h>  // Requires CHIP_FLASH_BANK_SIZE to be defined
#include <cstdint>

#include <vector>

#include <nos/NuggetClient.h>

namespace nos {

/**
 * Implementation of NuggetClient for Citadel.
 */
class LinuxCitadelClient : public NuggetClient {
 public:
  LinuxCitadelClient(int32_t freq, const std::string& serial);
  ~LinuxCitadelClient() override;

  void open() override;
  void close() override;
  bool isOpen() override;
  uint32_t callApp(uint32_t appId, uint16_t arg,
                   const std::vector<uint8_t>& request,
                   std::vector<uint8_t>& response) override;
 private:
  int32_t nosCoreFreq;
  std::string nosCoreSerial;
};

} // namespace nos

#endif // NOS_CITADEL_CLIENT_H