#include <nos/LinuxCitadelClient.h>

#include <cstdlib>
#include <stdint.h>
extern "C" {
#include "util/poker/driver.h"
}

#include <limits>

/* These are required as globals because of transport.c */
device_t* dev = nullptr;
int verbose = 0;

namespace nos {

LinuxCitadelClient::LinuxCitadelClient(int32_t freq, const std::string& serial)
    : nosCoreFreq(freq), nosCoreSerial(serial) {}

LinuxCitadelClient::~LinuxCitadelClient() {
  close();
}

void LinuxCitadelClient::open() {
  if (dev == nullptr) {
    dev = OpenDev(nosCoreFreq,
                  nosCoreSerial.size() ? nosCoreSerial.c_str() :
                  nullptr);
  }
}

void LinuxCitadelClient::close() {
  if (dev != nullptr) {
    CloseDev(dev);
  }
  dev = nullptr;
}

bool LinuxCitadelClient::isOpen() {
  return dev != nullptr;
}

uint32_t LinuxCitadelClient::callApp(uint32_t appId, uint16_t arg,
                                     const std::vector<uint8_t>& request,
                                     std::vector<uint8_t>& response) {
  if (request.size() > std::numeric_limits<uint32_t>::max()) {
    return APP_ERROR_TOO_MUCH;
  }
  const uint32_t requestSize = request.size();
  response.resize(response.capacity());
  uint32_t replySize = response.size();
  uint32_t status_code = call_application(
      appId, arg, request.data(), requestSize,
      response.data(), &replySize);
  response.resize(replySize);
  return status_code;
}

} // namespace nos
