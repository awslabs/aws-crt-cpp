#include "CanaryUtil.h"
#ifdef unix
#    include <sys/utsname.h>
#endif

using namespace Aws::Crt;

std::string CanaryUtil::GetPlatformName()
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
