#include "adapters/generic_moonraker_adapters.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include "utils/utils.h"
namespace elink
{
    GenericMoonrakerProtocol::GenericMoonrakerProtocol()
    {
    }

    std::string GenericMoonrakerProtocol::processConnectionUrl(const ConnectPrinterParams &connectParams)
    {
        int timeout = 5;
        if (connectParams.connectionTimeout / 1000 > 0)
        {
            timeout = connectParams.connectionTimeout / 1000;
        }
        std::string connectionUrl;

        try
        {
            auto urlInfo = UrlUtils::parseUrl(connectParams.host);
            if (urlInfo.isValid)
            {
                std::string endpoint;
                if (urlInfo.port != UrlUtils::getDefaultPort(urlInfo.scheme) && urlInfo.port != 0)
                {
                    endpoint = urlInfo.host + ":" + std::to_string(urlInfo.port);
                }
                else
                {
                    endpoint = urlInfo.host;
                }

                if (urlInfo.scheme == "https")
                {
                    // If the scheme is HTTPS, use the secure WebSocket port
                    connectionUrl = "wss://" + endpoint + "/websocket";
                }
                else
                {
                    connectionUrl = "ws://" + endpoint + "/websocket";
                }
            }

            std::string endpoint = UrlUtils::extractEndpoint(connectParams.host);
            httplib::Client cli(endpoint);
            cli.set_connection_timeout(timeout, 0);
            auto res = cli.Get("/access/oneshot_token");
            if (res && res->status == 200)
            {
                nlohmann::json jsonResponse = nlohmann::json::parse(res->body);
                if (jsonResponse.contains("result"))
                {
                    std::string token = jsonResponse["result"];
                    connectionUrl += "?token=" + token;
                    ELEGOO_LOG_DEBUG("Added oneshot token to WebSocket URL");
                }
                else
                {
                    ELEGOO_LOG_WARN("Oneshot token response missing 'result' field");
                }
            }
            else
            {
                ELEGOO_LOG_WARN("Failed to get oneshot token, status: {}",
                                res ? res->status : -1);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error getting oneshot token: {}", e.what());
        }

        return connectionUrl;
    }
} // namespace elink