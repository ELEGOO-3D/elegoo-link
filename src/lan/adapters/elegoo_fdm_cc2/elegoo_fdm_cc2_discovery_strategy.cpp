#include "adapters/elegoo_cc2_adapters.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include "utils/utils.h"
#include "utils/json_utils.h"
namespace elink
{
    std::string ElegooFdmCC2DiscoveryStrategy::getDiscoveryMessage() const
    {
        return R"({
        "id": 0,
        "method": 7000
        }
    )"; // Elegoo-specific discovery message
    }

    std::unique_ptr<PrinterInfo> ElegooFdmCC2DiscoveryStrategy::parseResponse(const std::string &response,
                                                                              const std::string &senderIp,
                                                                              int senderPort) const
    {
        try
        {
            auto jsonResponse = nlohmann::json::parse(response);

            // Check if it is an Elegoo printer response
            if (!jsonResponse.contains("id") || !jsonResponse.contains("result"))
            {
                return nullptr;
            }

            auto printerInfo = std::make_unique<PrinterInfo>();
            printerInfo->host = senderIp;
            printerInfo->brand = "Elegoo";
            printerInfo->manufacturer = "Elegoo";
            printerInfo->printerType = PrinterType::ELEGOO_FDM_CC2;

            jsonResponse = jsonResponse["result"];

            if (jsonResponse.contains("host_name"))
            {
                printerInfo->name = JsonUtils::safeGetString(jsonResponse, "host_name", "");
            }

            if (jsonResponse.contains("machine_model"))
            {
                printerInfo->model = JsonUtils::safeGetString(jsonResponse, "machine_model", "");
            }

            if (jsonResponse.contains("sn"))
            {
                printerInfo->serialNumber = JsonUtils::safeGetString(jsonResponse, "sn", "");
                printerInfo->mainboardId = printerInfo->serialNumber;
                printerInfo->printerId = PRINTER_ID_PREFIX_ELEGOO_LAN + printerInfo->serialNumber;
            }

            int tokenStatus = 0;
            if (jsonResponse.contains("token_status"))
            {
                if (jsonResponse["token_status"].is_number_integer())
                {
                    tokenStatus = jsonResponse.value("token_status", 0);
                }
                else if (jsonResponse["token_status"].is_boolean())
                {
                    bool b = JsonUtils::safeGetBool(jsonResponse, "token_status", false);
                    tokenStatus = b ? 1 : 0;
                }
            }
            const auto webUrl = getWebUrl(senderIp, 0);
            printerInfo->webUrl = webUrl;

            if (tokenStatus == 1)
            {
                printerInfo->authMode = "accessCode";
            }
            else
            {
                printerInfo->authMode = ""; // No authorization required
            }

            int lanStatus = 0;
            if (jsonResponse.contains("lan_status")) // 1: LAN only mode, 0: WAN mode (default)
            {
                if (jsonResponse["lan_status"].is_number_integer())
                {
                    lanStatus = jsonResponse.value("lan_status", 0);
                }
                else if (jsonResponse["lan_status"].is_boolean())
                {
                    bool b = JsonUtils::safeGetBool(jsonResponse, "lan_status", false);
                    lanStatus = b ? 1 : 0;
                }
            }

            printerInfo->networkMode = (lanStatus == 1) ? NetworkMode::LAN : NetworkMode::CLOUD;

            if (printerInfo->networkMode == NetworkMode::CLOUD)
            {
                printerInfo->printerId = PRINTER_ID_PREFIX_ELEGOO_CLOUD + printerInfo->serialNumber;
                printerInfo->authMode = "pinCode"; // CLOUD mode requires pinCode authorization
                printerInfo->webUrl = "";          // No web URL in CLOUD mode
            }

            return printerInfo;
        }
        catch (const std::exception &)
        {
            return nullptr; // Parsing failed, may not be an Elegoo printer
        }
    }

    std::string ElegooFdmCC2DiscoveryStrategy::getWebUrl(const std::string &host, int port) const
    {
        std::string webUrl;
        int phttpPort = webServerPort();
        if (isWebServerRunning() && phttpPort != 0)
        {
            webUrl = enableStaticWebServer() ? "http://127.0.0.1:" + std::to_string(phttpPort) : ""; // CC2 does not have a specific Web interface
        }
        else
        {
            std::string webPath = localStaticWebPath();
            if (!webPath.empty())
            {
                // Convert backslashes to forward slashes for URL compatibility
                std::replace(webPath.begin(), webPath.end(), '\\', '/');
                // Ensure no trailing slash
                if (webPath.back() == '/')
                {
                    webPath.pop_back();
                }
                return "file:///" + webPath + "/lan_service_web/index.html";
            }
        }
        return webUrl;
    }
} // namespace elink
