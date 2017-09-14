#include <nos/NuggetClient.h>

#include <application.h>

namespace nos {

#define ErrorString_helper(x) \
    case app_status::x: \
      return #x;

std::string NuggetClient::StatusCodeString(uint32_t code) {
  switch (code) {
    ErrorString_helper(APP_SUCCESS)
    ErrorString_helper(APP_ERROR_BOGUS_ARGS)
    ErrorString_helper(APP_ERROR_INTERNAL)
    ErrorString_helper(APP_ERROR_TOO_MUCH)
    ErrorString_helper(APP_ERROR_RPC)
    default:
      if (code >= APP_LINE_NUMBER_BASE && code < MAX_APP_STATUS) {
        return "APP_LINE_NUMBER " + std::to_string(code - APP_LINE_NUMBER_BASE);
      }
      if (code >= APP_SPECIFIC_ERROR && code < APP_LINE_NUMBER_BASE) {
        return "APP_SPECIFIC_ERROR " + std::to_string(APP_LINE_NUMBER_BASE) +
            " + " + std::to_string(code - APP_LINE_NUMBER_BASE);
      }

      return "unknown";
  }
}

}  // namespace nos
