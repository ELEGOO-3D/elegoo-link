#include "adapters/generic_moonraker_adapters.h"
#include "utils/logger.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>
#include <nlohmann/json.hpp>
#include "utils/utils.h"
namespace elink
{
    std::string GenericMoonrakerDiscoveryStrategy::getDiscoveryMessage() const
    {
        return "";
    }

    std::unique_ptr<PrinterInfo> GenericMoonrakerDiscoveryStrategy::parseResponse(const std::string &response,
                                                                                  const std::string &senderIp,
                                                                                  int senderPort) const
    {

        return nullptr;
    }

    std::string GenericMoonrakerDiscoveryStrategy::getWebUrl(const std::string &host, int port) const
    {
        if (host.find("file://") == 0 || host.find("http://") == 0 || host.find("https://") == 0)
        {
            return host;
        }
        return UrlUtils::extractEndpoint(host);
    }

}