#include "CanaryUtil.h"
#ifdef unix
#    include <sys/utsname.h>
#endif

using namespace Aws::Crt;

int32_t CanaryUtil::GetSwitchIndex(int argc, char *argv[], const char *switchName)
{
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(switchName, argv[i]))
        {
            return i;
        }
    }

    return -1;
}

bool CanaryUtil::HasSwitch(int argc, char *argv[], const char *switchName)
{
    return GetSwitchIndex(argc, argv, switchName) >= 0;
}

bool CanaryUtil::GetSwitchVariable(int argc, char *argv[], const char *switchName, String &outValue)
{
    int32_t switchIndex = GetSwitchIndex(argc, argv, switchName);

    if (switchIndex == -1)
    {
        return false;
    }

    int32_t switchValueIndex = switchIndex + 1;

    if (switchValueIndex < argc)
    {
        outValue = argv[switchValueIndex];
    }

    return true;
}

String CanaryUtil::GetPlatformName()
{
#ifdef WIN32
    return "windows";
#elif unix
    struct utsname unameResult;
    uname(&unameResult);
    return unameResult.sysname;
#else
    return "unknown";
#endif
}
