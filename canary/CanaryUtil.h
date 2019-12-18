#pragma once

#include <aws/crt/Types.h>
#include <inttypes.h>

class CanaryUtil
{
  public:
    CanaryUtil() = delete;

    static int32_t GetSwitchIndex(int argc, char *argv[], const char *switchName);

    static bool HasSwitch(int argc, char *argv[], const char *switchName);

    static bool GetSwitchVariable(int argc, char *argv[], const char *switchName, Aws::Crt::String &outValue);

    static Aws::Crt::String GetPlatformName();
};
