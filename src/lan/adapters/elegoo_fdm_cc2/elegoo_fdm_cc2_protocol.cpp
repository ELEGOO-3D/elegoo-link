#include "adapters/elegoo_cc2_adapters.h"
#include "utils/logger.h"
#include <chrono>
#include <random>
#include "utils/utils.h"
#include <httplib.h>

namespace elink
{
    ElegooCC2MqttProtocol::ElegooCC2MqttProtocol()
    {
        ELEGOO_LOG_DEBUG("ElegooCC2MqttProtocol created");
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        clientId_ = "1_PC_" + std::to_string(dis(gen));
        requestId_ = clientId_ + "_req"; // Use clientId as request_id
    }

    std::string ElegooCC2MqttProtocol::processConnectionUrl(const ConnectPrinterParams &connectParams)
    {
        auto urlInfo = UrlUtils::parseUrl(connectParams.host);
        if (urlInfo.isValid)
        {
            std::string connectionUrl = "tcp://" + urlInfo.host + ":1883";
            return connectionUrl;
        }
        return "";
    }

    std::string ElegooCC2MqttProtocol::getClientId(const ConnectPrinterParams &connectParams) const
    {
        return clientId_;
    }

    VoidResult ElegooCC2MqttProtocol::validateConnectionParams(const ConnectPrinterParams &connectParams) const
    {
        // Check if host is provided
        if (connectParams.host.empty())
        {
            ELEGOO_LOG_ERROR("Invalid connection parameters: host is empty");
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Host is required");
        }

        // Check if printer type is supported
        if (connectParams.printerType != PrinterType::ELEGOO_FDM_CC2)
        {
            ELEGOO_LOG_ERROR("Invalid connection parameters: Unsupported printer type");
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Unsupported printer type");
        }

        // If serial number is not provided, try to fetch it via HTTP
        if (connectParams.serialNumber.empty())
        {
            ELEGOO_LOG_WARN("Serial number is empty for CC2 printer, attempting to fetch via HTTP");
            try
            {
                int timeout = 5;
                if (connectParams.connectionTimeout / 1000 > 0)
                {
                    timeout = connectParams.connectionTimeout / 1000;
                }
                std::string endpoint = UrlUtils::extractEndpoint(connectParams.host);
                httplib::Client cli(endpoint);
                cli.set_connection_timeout(timeout, 0);
                cli.set_read_timeout(timeout, 0);
                cli.set_write_timeout(timeout, 0);
                std::string path = "/system/info";

                std::string accessCode = "123456";
                if (connectParams.authMode == "basic")
                {
                    accessCode = connectParams.password.empty() ? accessCode : connectParams.password;
                }
                else if (connectParams.authMode == "token")
                {
                    accessCode = connectParams.token.empty() ? accessCode : connectParams.token;
                }
                else if (connectParams.authMode == "accessCode")
                {
                    accessCode = connectParams.accessCode.empty() ? accessCode : connectParams.accessCode;
                }
                httplib::Headers headers = {{"X-Token", accessCode}};
                path += "?X-Token=" + accessCode;
                auto res = cli.Get(path.c_str(), headers);
                if (res && res->status == 200)
                {
                    ELEGOO_LOG_DEBUG("Received response from printer info API: {}", res->body);
                    nlohmann::json jsonResponse = nlohmann::json::parse(res->body);
                    if (jsonResponse.contains("error_code") && jsonResponse["error_code"].is_number())
                    {
                        int errorCode = jsonResponse["error_code"].get<int>();
                        if (errorCode != 0)
                        {
                            ELEGOO_LOG_ERROR("Error response from printer info API, error_code: {}", errorCode);
                            return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, StringUtils::formatErrorMessage(errorCode));
                        }
                    }
                    if (jsonResponse.contains("system_info") && jsonResponse["system_info"].is_object())
                    {
                        serialNumber_ = jsonResponse["system_info"].value("sn", "");
                        if (!serialNumber_.empty())
                        {
                            ELEGOO_LOG_INFO("Successfully retrieved printer serial number: {}", StringUtils::maskString(serialNumber_));
                            return VoidResult::Success();
                        }
                        else
                        {
                            ELEGOO_LOG_ERROR("Serial number field is empty in system_info");
                        }
                    }
                    else
                    {
                        ELEGOO_LOG_ERROR("System info response missing 'system_info' field");
                    }
                }
                else if (res && res->status == 401)
                {
                    ELEGOO_LOG_ERROR("Unauthorized access when retrieving printer info, status: 401");
                    return VoidResult::Error(ELINK_ERROR_CODE::INVALID_ACCESS_CODE, "Unauthorized access when retrieving printer info");
                }
                else
                {
                    std::string statusMsg = res ? std::to_string(res->status) : "No response";
                    ELEGOO_LOG_ERROR("Failed to get printer info for serial number retrieval, status: {}", statusMsg);
                    return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "Failed to get printer info for serial number retrieval");
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Exception while retrieving serial number: {}", e.what());
            }
            return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, "Exception while retrieving serial number");
        }
        else
        {
            serialNumber_ = connectParams.serialNumber;
        }

        return VoidResult::Success();
    }

    void ElegooCC2MqttProtocol::configureConnectionOptions(mqtt::connect_options &conn_opts,
                                                           const ConnectPrinterParams &connectParams)
    {
        // CC2-specific authentication logic
        if (connectParams.authMode == "basic")
        {
            // Use provided credentials or CC2 defaults
            std::string username = connectParams.username.empty() ? "elegoo" : connectParams.username;
            std::string password = connectParams.password.empty() ? "123456" : connectParams.password;

            conn_opts.set_user_name(username);
            conn_opts.set_password(password);

            ELEGOO_LOG_DEBUG("CC2 MQTT: Using basic auth with username: {}", username);
        }
        else if (connectParams.authMode == "token")
        {
            conn_opts.set_user_name("elegoo");
            std::string token = connectParams.token.empty() ? "123456" : connectParams.token;
            conn_opts.set_password(token);

            ELEGOO_LOG_DEBUG("CC2 MQTT: Using token auth");
        }
        else if (connectParams.authMode == "accessCode")
        {
            conn_opts.set_user_name("elegoo");
            std::string accessCode = connectParams.accessCode.empty() ? "123456" : connectParams.accessCode;
            conn_opts.set_password(accessCode);

            ELEGOO_LOG_DEBUG("CC2 MQTT: Using accessCode auth");
        }
        else if (connectParams.authMode == "pinCode")
        {
            conn_opts.set_user_name("elegoo");
            std::string pinCode = connectParams.pinCode.empty() ? "123456" : connectParams.pinCode;
            conn_opts.set_password(pinCode);

            ELEGOO_LOG_DEBUG("CC2 MQTT: Using pinCode auth");
        }
        else
        {
            // Default CC2 credentials
            conn_opts.set_user_name("elegoo");
            conn_opts.set_password("123456");

            ELEGOO_LOG_DEBUG("CC2 MQTT: Using default credentials");
        }
    }

    std::vector<std::string> ElegooCC2MqttProtocol::getSubscriptionTopics(const ConnectPrinterParams &connectParams) const
    {
        // Use cached serialNumber_ if available, otherwise use connectParams
        const std::string &sn = serialNumber_.empty() ? connectParams.serialNumber : serialNumber_;
        return {
            // Basic status topic
            "elegoo/" + sn + "/" + clientId_ + "/api_response",
            "elegoo/" + sn + "/api_status",
            // Printer status update topic
            "elegoo/" + sn + "/" + requestId_ + "/register_response",
        };
    }

    std::string ElegooCC2MqttProtocol::getCommandTopic(const ConnectPrinterParams &connectParams, const std::string &commandType) const
    {
        // Use cached serialNumber_ if available, otherwise use connectParams
        const std::string &sn = serialNumber_.empty() ? connectParams.serialNumber : serialNumber_;
        // CC2 uses a unified command topic
        return "elegoo/" + sn + "/" + clientId_ + "/api_request";
    }

    bool ElegooCC2MqttProtocol::requiresRegistration() const
    {
        return true; // CC2 printers require registration
    }

    bool ElegooCC2MqttProtocol::performRegistration(const ConnectPrinterParams &connectParams,
                                                    const std::string &clientId,
                                                    std::function<bool(const std::string &, const std::string &)> sendMessageCallback)
    {
        try
        {
            // Use cached serialNumber_ if available, otherwise use connectParams
            const std::string &sn = serialNumber_.empty() ? connectParams.serialNumber : serialNumber_;

            // Construct registration message
            nlohmann::json registerMsg;
            registerMsg["client_id"] = clientId;
            registerMsg["request_id"] = requestId_; // Use clientId as request_id

            std::string topic = "elegoo/" + sn + "/api_register";
            std::string message = registerMsg.dump();

            ELEGOO_LOG_INFO("[MQTT] Sending registration request to {}: {}", StringUtils::maskString(topic), message);

            return sendMessageCallback(topic, message);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[MQTT] Failed to perform registration: {}", e.what());
            return false;
        }
    }

    bool ElegooCC2MqttProtocol::isRegistrationMessage(const std::string &topic, const std::string &message) const
    {
        // Check if topic matches registration response format: elegoo/<sn>/<request_id>/register_response
        std::string expectedTopicSuffix = "/" + requestId_ + "/register_response";
        if (topic.find(expectedTopicSuffix) == std::string::npos)
        {
            return false; // Not a registration response message
        }
        return true;
    }

    bool ElegooCC2MqttProtocol::validateRegistrationResponse(const std::string &topic,
                                                             const std::string &message,
                                                             const std::string &clientId,
                                                             ELINK_ERROR_CODE &errorCode,
                                                             std::string &errorMessage)
    {
        try
        {
            errorCode = ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
            // Parse response message
            nlohmann::json response = nlohmann::json::parse(message);

            // Validate client_id match
            if (!response.contains("client_id") || response["client_id"] != clientId)
            {
                ELEGOO_LOG_ERROR("[MQTT] Registration response client_id mismatch. Expected: {}, Got: {}",
                                 clientId, response.value("client_id", ""));
                errorMessage = "Client ID mismatch";
                return false;
            }

            // Check error status
            std::string error = response.value("error", "fail");
            if (error == "ok")
            {
                ELEGOO_LOG_INFO("[MQTT] Printer registration successful for client_id: {}", clientId);
                errorCode = ELINK_ERROR_CODE::SUCCESS;
                errorMessage.clear();
                return true;
            }
            if (error.find("too many clients") != std::string::npos)
            {
                ELEGOO_LOG_WARN("[MQTT] Printer registration failed: client_id: {}. Error: {}", clientId, error);
                errorCode = ELINK_ERROR_CODE::PRINTER_CONNECTION_LIMIT_EXCEEDED;
                errorMessage = "Connection limit exceeded";
                return false;
            }
            else
            {
                ELEGOO_LOG_ERROR("Printer registration failed: {}", error);
                errorMessage = error;
                return false;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[MQTT] Failed to validate registration response: {}", e.what());
            return false;
        }
    }

    int ElegooCC2MqttProtocol::getRegistrationTimeoutMs() const
    {
        return 3000; // 3 seconds for CC2 registration
    }

    void ElegooCC2MqttProtocol::handleMessage(const std::string &topic, const std::string &payload)
    {
        // CC2-specific message handling logic
    }

    bool ElegooCC2MqttProtocol::isHeartbeatEnabled() const
    {
        return true;
    }

    int ElegooCC2MqttProtocol::getHeartbeatIntervalSeconds() const
    {
        return 10; // If enabled, use 10 seconds
    }

    std::string ElegooCC2MqttProtocol::createHeartbeatMessage() const
    {
        return "{\"type\":\"PING\"}";
    }

    bool ElegooCC2MqttProtocol::handleHeartbeatResponse(const std::string &payload)
    {
        try
        {
            nlohmann::json response = nlohmann::json::parse(payload, nullptr, false);
            return response.contains("type") && response["type"] == "PONG";
        }
        catch (...)
        {
            return false;
        }
    }

    std::string ElegooCC2MqttProtocol::getHeartbeatTopic(const ConnectPrinterParams &connectParams) const
    {
        return getCommandTopic(connectParams); // Use command topic for heartbeat
    }

    int ElegooCC2MqttProtocol::getHeartbeatTimeoutSeconds() const
    {
        return 65; // 65 seconds timeout for CC2
    }

    // ============ Private helper methods ============

    std::string ElegooCC2MqttProtocol::generateRequestId() const
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(1000, 9999);

        return std::to_string(dis(gen));
    }
} // namespace elink
