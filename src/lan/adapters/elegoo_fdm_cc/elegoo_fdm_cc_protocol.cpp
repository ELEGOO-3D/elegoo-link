#include "adapters/elegoo_cc_adapters.h"
#include "utils/utils.h"
namespace elink
{
    ElegooFdmCCProtocol::ElegooFdmCCProtocol()
    {
    }

    std::string ElegooFdmCCProtocol::processConnectionUrl(const ConnectPrinterParams &connectParams)
    {
        auto urlInfo = UrlUtils::parseUrl(connectParams.host);
        if (urlInfo.isValid)
        {
            if (urlInfo.scheme == "https")
            {
                // If the scheme is HTTPS, use the secure WebSocket port
                std::string connectionUrl = "wss://" + urlInfo.host + ":3030" + "/websocket";
                return connectionUrl;
            }
            std::string connectionUrl = "ws://" + urlInfo.host + ":3030" + "/websocket";
            return connectionUrl;
        }
        return "";
    }
} // namespace elink