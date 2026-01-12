#include "protocols/error_handler.h"
#include <mqtt/async_client.h>
namespace elink
{
    ELINK_ERROR_CODE ErrorHandler::mapWebSocketErrorCode(const std::string &errorMessage)
    {
        if (errorMessage.find("401") != std::string::npos ||
            errorMessage.find("403") != std::string::npos)
        {
            return ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
        }
        else if (errorMessage.find("404") != std::string::npos)
        {
            return ELINK_ERROR_CODE::PRINTER_NOT_FOUND;
        }
        else if (errorMessage.find("500") != std::string::npos)
        {
            return ELINK_ERROR_CODE::UNKNOWN_ERROR;
        }
        else if (errorMessage.find("timeout") != std::string::npos)
        {
            return ELINK_ERROR_CODE::OPERATION_TIMEOUT;
        }
        else if (errorMessage.find("network") != std::string::npos ||
                 errorMessage.find("resolve") != std::string::npos)
        {
            return ELINK_ERROR_CODE::NETWORK_ERROR;
        }
        return ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
    }

    ELINK_ERROR_CODE ErrorHandler::mapMqttReasonCode(const int &reasonCode)
    {
        // Map MQTT reason codes to ELINK_ERROR_CODE
        switch (reasonCode)
        {
        case mqtt::ReasonCode::SUCCESS:
            return ELINK_ERROR_CODE::SUCCESS;
        case mqtt::ReasonCode::NOT_AUTHORIZED:
            return ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
        case mqtt::ReasonCode::BAD_USER_NAME_OR_PASSWORD:
            return ELINK_ERROR_CODE::INVALID_USERNAME_OR_PASSWORD;
        default:
            return ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
        }
    }

    ELINK_ERROR_CODE ErrorHandler::mapMqttReturnCode(const int &returnCode, const std::string &authMode)
    {
        // 0	Connection Accepted
        // 1	Unacceptable Protocol Version
        // 2	Identifier Rejected
        // 3	Server Unavailable
        // 4	Bad Username or Password
        // 5	Not Authorized
        // Map MQTT return codes to ELINK_ERROR_CODE
        switch (returnCode)
        {
        case 0:
            return ELINK_ERROR_CODE::SUCCESS;
        case 1:
            return ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
        case 2:
            return ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
        case 3:
            return ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR;
        case 4:
        case 5:
            if (authMode == "basic")
            {
                return ELINK_ERROR_CODE::INVALID_USERNAME_OR_PASSWORD;
            }
            else if (authMode == "token")
            {
                return ELINK_ERROR_CODE::INVALID_TOKEN;
            }
            else if (authMode == "accessCode")
            {
                return ELINK_ERROR_CODE::INVALID_ACCESS_CODE;
            }
            else if (authMode == "pinCode")
            {
                return ELINK_ERROR_CODE::INVALID_PIN_CODE;
            }
            return ELINK_ERROR_CODE::INVALID_ACCESS_CODE;
        default:
            return ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
        }
    }

    VoidResult ErrorHandler::createConnectionFailure(
        ELINK_ERROR_CODE errorCode,
        const std::string &title,
        const std::string &details,
        std::chrono::steady_clock::time_point startTime)
    {
        std::string message = title;
        if (!details.empty())
        {
            message += ": " + details;
        }
        return VoidResult::Error(errorCode, message);
    }

    VoidResult ErrorHandler::createTimeoutFailure(
        const std::string &protocolName,
        std::chrono::steady_clock::time_point startTime)
    {
        return VoidResult::Error(
            ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR,
            protocolName + " connection timeout");
    }

} // namespace elink
