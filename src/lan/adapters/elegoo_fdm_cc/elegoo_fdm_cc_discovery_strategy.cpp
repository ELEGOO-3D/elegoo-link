#include "adapters/elegoo_cc_adapters.h"
#include "utils/logger.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>
#include <nlohmann/json.hpp>
#include "utils/utils.h"
namespace elink
{
    // ========== ElegooFdmCCDiscoveryStrategy Implementation ==========

    std::string ElegooFdmCCDiscoveryStrategy::getDiscoveryMessage() const
    {
        return "M99999"; // Elegoo specific discovery message
    }

    std::unique_ptr<PrinterInfo> ElegooFdmCCDiscoveryStrategy::parseResponse(const std::string &response,
                                                                             const std::string &senderIp,
                                                                             int senderPort) const
    {
        try
        {
            auto jsonResponse = nlohmann::json::parse(response);

            // Check if it is an Elegoo printer response
            if (!jsonResponse.contains("Id") || !jsonResponse.contains("Data"))
            {
                return nullptr;
            }

            auto printerInfo = std::make_unique<PrinterInfo>();
            printerInfo->host = senderIp;
            printerInfo->brand = "Elegoo";
            printerInfo->printerType = PrinterType::ELEGOO_FDM_CC;
            printerInfo->authMode = getSupportedAuthMode();
            jsonResponse = jsonResponse["Data"];

            if (jsonResponse.contains("Name"))
            {
                printerInfo->name = jsonResponse.value("Name", "");
            }

            if (jsonResponse.contains("MachineName"))
            {
                printerInfo->model = jsonResponse.value("MachineName", "");
            }

            if (jsonResponse.contains("MainboardID"))
            {
                printerInfo->printerId = PRINTER_ID_PREFIX_ELEGOO_LAN + jsonResponse.value("MainboardID", "");
                printerInfo->mainboardId = jsonResponse.value("MainboardID", "");
            }

            if (jsonResponse.contains("FirmwareVersion"))
            {
                printerInfo->firmwareVersion = jsonResponse.value("FirmwareVersion", "");
                // Remove "V" at the beginning of version
                if (printerInfo->firmwareVersion.find_first_of('V') == 0)
                {
                    printerInfo->firmwareVersion.erase(0, 1);
                }
            }
            printerInfo->manufacturer = "Elegoo";

            const auto webUrl = getWebUrl(senderIp, 0);
            printerInfo->webUrl = webUrl;
            return printerInfo;
        }
        catch (const std::exception &)
        {
            return nullptr; // Parsing failed, may not be an Elegoo printer
        }
    }

    std::string ElegooFdmCCDiscoveryStrategy::getWebUrl(const std::string &host, int port) const
    {
        if (host.find("file://") == 0 || host.find("http://") == 0 || host.find("https://") == 0)
        {
            return host;
        }
        return UrlUtils::extractEndpoint(host);
    }
}