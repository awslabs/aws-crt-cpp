#pragma once

#include <aws/crt/Types.h>
#include <inttypes.h>

class CanaryUtil
{
  public:
    CanaryUtil() = delete;

    static Aws::Crt::String GetPlatformName();
};
