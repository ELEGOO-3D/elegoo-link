#pragma once
#include "../biz.h"
#include "message.h"
#include "version.h"
namespace elink 
{
    #define ELEGOO_LINK_USER_AGENT "ElegooLink/" ELEGOO_LINK_SDK_VERSION
    bool enableStaticWebServer();
    int webServerPort();
    bool isWebServerRunning();
    std::string localStaticWebPath();
    std::string cloudStaticWebPath();

    #define PRINTER_ID_PREFIX_ELEGOO_LAN "lan_"
    #define PRINTER_ID_PREFIX_ELEGOO_CLOUD "cloud_"
    namespace internal
    {

    } // namespace internal
} // namespace elink
