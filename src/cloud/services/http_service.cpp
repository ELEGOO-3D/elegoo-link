#include "services/http_service.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include "private_config.h"
#include "app_utils.h"
#include "types/internal/internal.h"
#include "utils/json_utils.h"
#define APP_DEFAULT_REGION "cn"
namespace elink
{
    std::string HttpService::buildUrlPath(const std::string &path)
    {
        return path;
    }
    HttpService::HttpService()
    {
        m_region = APP_DEFAULT_REGION;
    }

    HttpService::~HttpService()
    {
        cleanup();
        m_httpClient.reset();
    }

    VoidResult HttpService::initialize(std::string region, std::string userAgent, std::string baseUrl, std::string caCertPath)
    {
        if (m_initialized.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_IN_PROGRESS, "HTTP service is already initialized");
        }

        try
        {
            m_userAgent = userAgent;
            m_caCertPath = caCertPath;
            // If region is empty, default to "cn"
            if (!region.empty())
            {
                m_region = region;
            }
            else
            {
                m_region = APP_DEFAULT_REGION;
            }
            auto result = initializeClient(userAgent, baseUrl, caCertPath);
            if (!result.isSuccess())
            {
                return result;
            }

            m_initialized.store(true);
            ELEGOO_LOG_INFO("HTTP service initialization completed");
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "HTTP service initialization failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void HttpService::cleanup()
    {
        if (!m_initialized.load())
        {
            return;
        }

        try
        {
            cleanupClient();
            m_credential = HttpCredential{};
            m_initialized.store(false);
            ELEGOO_LOG_INFO("HTTP service cleanup completed");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred during HTTP service cleanup: {}", e.what());
        }
    }

    bool HttpService::isInitialized() const
    {
        return m_initialized.load();
    }

    VoidResult HttpService::setCredential(const HttpCredential &credential)
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        m_credential = credential;

        if (m_httpClient)
        {
            m_httpClient->setBearerToken(credential.accessToken);
            ELEGOO_LOG_DEBUG("HTTP credential updated");
        }
        return VoidResult::Success();
    }

    VoidResult HttpService::clearCredential()
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        m_credential = HttpCredential{};

        if (m_httpClient)
        {
            m_httpClient->clearBearerToken();
            ELEGOO_LOG_DEBUG("HTTP credential cleared");
        }
        return VoidResult::Success();
    }

    const HttpCredential &HttpService::getCredential() const
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        return m_credential;
    }

    VoidResult HttpService::setRegion(const SetRegionParams &params)
    {
        std::string regionUrl = params.baseUrl;
        std::string region = params.region;

        // If region is empty, default to "cn"
        if (region.empty())
        {
            region = APP_DEFAULT_REGION;
        }

        // Only use default region URL when baseUrl is empty
        if (regionUrl.empty())
        {
            std::transform(region.begin(), region.end(), region.begin(), ::tolower);
            if (region == "china" || region == "cn")
            {
                regionUrl = ELEGOO_CHINA_IOT_URL;
            }
            else
            {
                regionUrl = ELEGOO_GLOBAL_IOT_URL;
            }

            if (regionUrl.empty())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Region URL is not configured");
            }
        }

        m_region = region;
        m_baseUrl = regionUrl;

        try
        {
            HttpConfig httpConfig;
            httpConfig.userAgent = m_userAgent;
            httpConfig.caCertPath = m_caCertPath;
            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_httpClient = std::make_shared<HttpClient>(regionUrl, httpConfig);
            ELEGOO_LOG_INFO("HTTP client region set to {} with URL: {}", params.region, regionUrl);
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "Failed to set HTTP client region: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    VoidResult HttpService::serverErrorToNetworkError(int serverCode)
    {
        //   ErrorCode SUCCESS = new ErrorCode(0, "Success");
        //   ErrorCode BAD_REQUEST = new ErrorCode(400, "Bad Request");
        //   ErrorCode UNAUTHORIZED = new ErrorCode(401, "Unauthorized");
        //   ErrorCode FORBIDDEN = new ErrorCode(403, "Forbidden");
        //   ErrorCode NOT_FOUND = new ErrorCode(404, "Not Found");
        //   ErrorCode METHOD_NOT_ALLOWED = new ErrorCode(405, "Method Not Allowed");
        //   ErrorCode LOCKED = new ErrorCode(423, "Request failed, please try again later");
        //   ErrorCode TOO_MANY_REQUESTS = new ErrorCode(429, "Too many requests, please try again later");
        //   ErrorCode INTERNAL_SERVER_ERROR = new ErrorCode(500, "Internal server error");
        //   ErrorCode NOT_IMPLEMENTED = new ErrorCode(501, "Feature not implemented");
        //   ErrorCode ERROR_CONFIGURATION = new ErrorCode(502, "Invalid configuration");
        //   ErrorCode REPEATED_REQUESTS = new ErrorCode(900, "Repeated requests, please try again later");
        //   ErrorCode DEMO_DENY = new ErrorCode(901, "Demo mode, write operations are prohibited");
        //   ErrorCode UNKNOWN = new ErrorCode(999, "Unknown error");
        std::string outMessage;
        switch (serverCode)
        {
        case 0:
            outMessage = "Success";
            return VoidResult::Success();
        case 401:
            outMessage = "Unauthorized";
            return VoidResult::Error(ELINK_ERROR_CODE::SERVER_UNAUTHORIZED, outMessage);
        case 403:
            outMessage = "Forbidden";
            return VoidResult::Error(ELINK_ERROR_CODE::SERVER_FORBIDDEN, outMessage);
        case 429:
            outMessage = "Too Many Requests";
            return VoidResult::Error(ELINK_ERROR_CODE::SERVER_TOO_MANY_REQUESTS, outMessage);
        case 30010:
            outMessage = "Invalid pin Code";
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PIN_CODE, outMessage);

        default:
            outMessage = "Unknown Error";
            return VoidResult::Error(ELINK_ERROR_CODE::SERVER_UNKNOWN_ERROR, StringUtils::formatErrorMessage(serverCode));
        }
    }
    VoidResult HttpService::handleResponse(const HttpResponse &result)
    {
        int statusCode = result.statusCode;
        if (statusCode >= 200 && statusCode < 300)
        {
            return VoidResult::Success();
        }
        else
        {
            VoidResult r = serverErrorToNetworkError(statusCode);
            std::string errorMsg = "HTTP request failed with status code: " + std::to_string(statusCode);
            ELEGOO_LOG_ERROR(errorMsg);
            return r;
        }
    }
    void HttpService::updatePrinters(const std::vector<PrinterInfo> &printers)
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        m_printers = printers;
    }

    bool HttpService::shouldRefreshToken() const
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);

        if (m_credential.accessToken.empty())
        {
            return false;
        }

        auto currentTime = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

        if (m_credential.accessTokenExpireTime > 0 &&
            (m_credential.accessTokenExpireTime - currentTime) < TOKEN_REFRESH_THRESHOLD_SECONDS)
        {
            return true;
        }

        return false;
    }

    BizResult<HttpCredential> HttpService::refreshCredential(const HttpCredential &credential)
    {
        auto httpClient = getHttpClient(true);
        if (!httpClient)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }
        std::string refreshToken = credential.refreshToken;
        std::string accessToken = credential.accessToken;
        if (refreshToken.empty() || accessToken.empty())
        {
            refreshToken = m_credential.refreshToken;
            accessToken = m_credential.accessToken;
        }

        if (refreshToken.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "No refresh token available");
        }

        try
        {
            nlohmann::json requestBody;
            requestBody["refreshToken"] = refreshToken;
            requestBody["clientId"] = "Slicer";
            httpClient->setBearerToken(accessToken);
            BizResult<HttpResponse> result = httpClient->post(buildUrlPath("/api/v1/account-center-server/account-auth/token/refresh"), requestBody);
            if (!result.isSuccess())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to refresh token: " + result.message);
            }

            const auto &response = result.value();
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);

            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);
            if (code == 0 && jsonResponse.contains("data") && jsonResponse["data"].is_object())
            {
                nlohmann::json data = jsonResponse["data"];

                m_credential.accessToken = JsonUtils::safeGetString(data, "accessToken", "");
                m_credential.refreshToken = JsonUtils::safeGetString(data, "refreshToken", "");
                m_credential.accessTokenExpireTime = JsonUtils::safeGetInt64(data, "expiresTime", 0);
                m_credential.refreshTokenExpireTime = JsonUtils::safeGetInt64(data, "refreshExpiresTime", 0);
                m_credential.userId = JsonUtils::safeGetString(data, "accountId", "");

                m_httpClient->setBearerToken(m_credential.accessToken);

                ELEGOO_LOG_INFO("HTTP token refreshed successfully");
                return BizResult<HttpCredential>::Ok(m_credential);
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Exception during token refresh: " + std::string(e.what()));
        }
    }

    BizResult<UserInfo> HttpService::getUserInfo()
    {
        auto httpClient = getHttpClient(true);
        if (!httpClient)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/account-center-server/account-info/account"));

        if (!result.isSuccess())
        {
            return VoidResult::Error(result.code, result.message);
        }
        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return BizResult<UserInfo>::Error(handleResult.code, handleResult.message);
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];
                    UserInfo userInfo;
                    userInfo.userId = JsonUtils::safeGetString(data, "id", "");
                    userInfo.phone = JsonUtils::safeGetString(data, "phone", "");
                    userInfo.email = JsonUtils::safeGetString(data, "email", "");
                    userInfo.nickName = JsonUtils::safeGetString(data, "nickname", "");
                    userInfo.avatar = JsonUtils::safeGetString(data, "avatarUrl", "");
                    return BizResult<UserInfo>::Ok(std::move(userInfo));
                }
                else
                {
                    return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "No data in user info response");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Exception during parsing user info: " + std::string(e.what()));
        }
    }

    VoidResult HttpService::logout()
    {
        auto httpClient = getHttpClient(true);
        if (!httpClient)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        BizResult<HttpResponse> result = httpClient->post(buildUrlPath("/api/v1/account-center-server/account-auth/logout"), nlohmann::json{},
                                                          {{"Content-Type", "application/json"}}, RequestTimeoutConfig{5000, 5000});

        // Clear local authentication information
        m_credential = HttpCredential{};
        if (m_httpClient)
        {
            m_httpClient->clearBearerToken();
        }
        if (!result.isSuccess())
        {
            return VoidResult::Error(result.code, result.message);
        }
        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                ELEGOO_LOG_INFO("User logged out successfully");
                return VoidResult::Success();
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Exception during parsing logout response: " + std::string(e.what()));
        }
    }

    GetPrinterListResult HttpService::getPrinters()
    {
        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get printer list");
            return GetPrinterListResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/device/list"));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get device list: {}", result.message);
            return GetPrinterListResult::Error(result.code, result.message);
        }
        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_array())
                {
                    std::vector<PrinterInfo> printers;
                    for (const auto &item : jsonResponse["data"])
                    {
                        PrinterInfo printer = generatePrinterInfo(JsonUtils::safeGetString(item, "serialNo", ""),
                                                                  JsonUtils::safeGetString(item, "pcode", ""),
                                                                  JsonUtils::safeGetString(item, "deviceName", ""));
                        printers.push_back(printer);
                    }
                    GetPrinterListResult result;
                    result.data = GetPrinterListData();
                    result.data->printers = printers;
                    return result;
                }
                else
                {
                    ELEGOO_LOG_ERROR("No printer data in response");
                    return GetPrinterListResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "No printer data in response");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to get printer list, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse printer list response: {}", e.what());
            return GetPrinterListResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    BizResult<AgoraCredential> HttpService::getAgoraCredential()
    {
        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get Agora credential");
            return BizResult<AgoraCredential>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/device/list/agora-token?source=slicer"));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get Agora credential: {}", result.message);
            return BizResult<AgoraCredential>::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];
                    if (data.contains("agoraToken") && data["agoraToken"].is_object())
                    {
                        nlohmann::json agoraToken = data["agoraToken"];

                        AgoraCredential credential;
                        credential.userId = std::to_string(JsonUtils::safeGetInt(agoraToken, "userId", 0));
                        credential.rtcUserId = std::to_string(JsonUtils::safeGetInt(agoraToken, "rtcUserId", 0));
                        credential.rtcToken = JsonUtils::safeGetString(agoraToken, "rtcToken", "");
                        credential.rtmToken = JsonUtils::safeGetString(agoraToken, "rtmToken", "");
                        credential.rtcTokenExpireTime = JsonUtils::safeGetInt(agoraToken, "rtcExpiresIn", 0);
                        credential.rtmTokenExpireTime = JsonUtils::safeGetInt(agoraToken, "rtmExpiresIn", 0);
                        credential.rtmUserId = JsonUtils::safeGetString(agoraToken, "rtmUserId", "");

                        ELEGOO_LOG_INFO("Parsed Agora credential details: userId={}, rtmUserId={}", StringUtils::maskString(credential.userId), StringUtils::maskString(credential.rtmUserId));
                        return BizResult<AgoraCredential>::Ok(std::move(credential));
                    }
                    else
                    {
                        ELEGOO_LOG_ERROR("Agora token not found in response data");
                        return BizResult<AgoraCredential>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Agora token not found in response data");
                    }
                }
                else
                {
                    ELEGOO_LOG_ERROR("No data in Agora credential response");
                    return BizResult<AgoraCredential>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "No data in response");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to get Agora credential, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse Agora credential response: {}", e.what());
            return BizResult<AgoraCredential>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    BizResult<MqttCredential> HttpService::getMqttCredential()
    {
        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get MQTT credential");
            return BizResult<MqttCredential>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

#ifdef _WIN32
        std::string suffix = "win";
#elif __APPLE__
        std::string suffix = "mac";
#elif __linux__
        std::string suffix = "linux";
#else
        std::string suffix = "unknown";
#endif

        std::string mqttClientId = "elegooslicer_" + suffix + "_" + m_credential.userId; // + "_" + CryptoUtils::getCachedMachineId();

        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/mqtt-link/mqtt-client?mqttClientId=" + UrlUtils::UrlEncode(mqttClientId)));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get MQTT credential: {}", result.message);
            return BizResult<MqttCredential>::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];

                    MqttCredential credential;
                    credential.host = JsonUtils::safeGetString(data, "host", "");
                    credential.mqttClientId = JsonUtils::safeGetString(data, "mqttClientId", "");
                    credential.mqttPassword = JsonUtils::safeGetString(data, "mqttPassword", "");
                    credential.mqttUserName = JsonUtils::safeGetString(data, "mqttUserName", "");
                    credential.publishAuthorization = JsonUtils::safeGetString(data, "publishAuthorization", "");
                    credential.subscribeAuthorization = JsonUtils::safeGetString(data, "subscribeAuthorization", "");

                    ELEGOO_LOG_INFO("Parsed MQTT credential details: host={}, clientId={}, userName={}", credential.host, StringUtils::maskString(credential.mqttClientId), StringUtils::maskString(credential.mqttUserName));
                    return BizResult<MqttCredential>::Ok(std::move(credential));
                }
                else
                {
                    ELEGOO_LOG_ERROR("MQTT credential not found in response data");
                    return BizResult<MqttCredential>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "MQTT credential not found in response data");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to get MQTT credential, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse MQTT credential response: {}", e.what());
            return BizResult<MqttCredential>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    VoidResult HttpService::initializeClient(std::string userAgent, std::string baseUrl, std::string caCertPath)
    {
        try
        {
            if (baseUrl.empty())
            {
                std::string regionUrl;
                std::string region = m_region;
                std::transform(region.begin(), region.end(), region.begin(), ::tolower);
                if (region == "china" || region == "cn")
                {
                    regionUrl = ELEGOO_CHINA_IOT_URL;
                }
                else
                {
                    regionUrl = ELEGOO_GLOBAL_IOT_URL;
                }
                baseUrl = regionUrl;
            }

            m_baseUrl = baseUrl;
            HttpConfig httpConfig;
            httpConfig.userAgent = userAgent;
            httpConfig.caCertPath = caCertPath;

            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_httpClient = std::make_shared<HttpClient>(baseUrl, httpConfig);
            m_httpClient->setBearerToken(m_credential.accessToken);

            ELEGOO_LOG_INFO("HTTP client initialized with base URL: {}", baseUrl);
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "HTTP client initialization failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void HttpService::cleanupClient()
    {
        try
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            if (m_httpClient)
            {
                m_httpClient->stop();
                ELEGOO_LOG_DEBUG("Cleaning up HTTP client");
                // m_httpClient.reset();
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while cleaning up HTTP client: {}", e.what());
        }
    }

    BizResult<HttpService::PinCodeDetails> HttpService::checkPincode(const std::string &printerModel, const std::string &pinCode)
    {
        if (printerModel.empty())
        {
            return BizResult<PinCodeDetails>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Model code cannot be empty");
        }

        if (pinCode.empty())
        {
            return BizResult<PinCodeDetails>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Pin code cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot check pin code");
            return BizResult<PinCodeDetails>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/device/pincode/detail?pcode=" + UrlUtils::UrlEncode(printerModel) + "&pincode=" + UrlUtils::UrlEncode(pinCode)));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to check pin code: {}", result.message);
            return BizResult<PinCodeDetails>::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                ELEGOO_LOG_INFO("Printer bound successfully");
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    HttpService::PinCodeDetails details;
                    nlohmann::json data = jsonResponse["data"];
                    std::string serialNumber = JsonUtils::safeGetString(data, "serialNo", "");
                    details.serialNumber = serialNumber;
                    details.model = printerModel;
                    details.pinCode = JsonUtils::safeGetString(data, "pincode", "");
                    details.expireTime = JsonUtils::safeGetInt64(data, "expiresIn", 0);
                    return BizResult<HttpService::PinCodeDetails>::Ok(details);
                }
                else
                {
                    ELEGOO_LOG_ERROR("No data in pin code details response");
                    return BizResult<HttpService::PinCodeDetails>::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "No data in response");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to bind printer, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse bind printer response: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    BizResult<std::string> HttpService::bindPrinter(const BindPrinterParams &params, bool manualConfirm)
    {
        if (params.model.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Model code cannot be empty");
        }

        if (params.pinCode.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Pin code cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot bind printer");
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        std::string serialNumber = params.serialNumber;
        if (serialNumber.empty())
        {
            ELEGOO_LOG_ERROR("Serial number is required for binding printer");
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Serial number is required");
        }

        nlohmann::json requestBody;
        requestBody["serialNo"] = serialNumber;
        requestBody["pcode"] = params.model;
        requestBody["pincode"] = params.pinCode;
        requestBody["deviceName"] = params.name;
        requestBody["manualConfirm"] = manualConfirm;

        BizResult<HttpResponse> result = httpClient->post(buildUrlPath("/api/v1/device-management-server/device/bind"), requestBody);
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to bind printer: {}", result.message);
            return VoidResult::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                ELEGOO_LOG_INFO("Printer bound successfully");
                return BizResult<std::string>::Ok(serialNumber);
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to bind printer, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse bind printer response: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    VoidResult HttpService::unbindPrinter(const UnbindPrinterParams &params)
    {
        if (params.serialNumber.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Serial number cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot unbind printer");
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        std::string serialNumber = params.serialNumber;

        BizResult<HttpResponse> result = httpClient->del(buildUrlPath("/api/v1/device-management-server/device/unbind?serialNo=" + UrlUtils::UrlEncode(serialNumber)));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to unbind printer: {}", result.message);
            return VoidResult::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                ELEGOO_LOG_INFO("Printer unbound successfully");
                return VoidResult::Success();
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to unbind printer, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse unbind printer response: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    GetFileListResult HttpService::getFileList(const GetFileListParams &params)
    {
        if (params.printerId.empty())
        {
            return GetFileListResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get file list");
            return GetFileListResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }
        std::string serialNumber = getSerialNumberByPrinterId(params.printerId);
        if (serialNumber.empty())
        {
            ELEGOO_LOG_ERROR("No serial number found for printer ID: {}", StringUtils::maskString(params.printerId));
            return GetFileListResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "No serial number found for printer ID");
        }

        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/local-file/page?serialNo=" + UrlUtils::UrlEncode(serialNumber) + "&pageNo=" + std::to_string(params.pageNumber) + "&pageSize=" + std::to_string(params.pageSize)));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get file list: {}", result.message);
            return GetFileListResult::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];
                    GetFileListData fileListData;
                    fileListData.totalFiles = JsonUtils::safeGetInt(data, "total", 0);
                    if (data.contains("list") && data["list"].is_array())
                    {
                        for (const auto &item : data["list"])
                        {
                            FileDetail fileInfo;
                            fileInfo.fileName = JsonUtils::safeGetString(item, "filename", "");
                            fileInfo.printTime = JsonUtils::safeGetInt64(item, "printTime", 0);
                            fileInfo.layer = JsonUtils::safeGetInt(item, "layer", 0);
                            fileInfo.layerHeight = JsonUtils::safeGetDouble(item, "layerHeight", 0.0);
                            fileInfo.thumbnail = JsonUtils::safeGetString(item, "thumbnail", "");
                            fileInfo.size = JsonUtils::safeGetInt64(item, "size", 0);
                            fileInfo.createTime = JsonUtils::safeGetInt64(item, "createTime", 0);
                            fileInfo.totalFilamentUsed = JsonUtils::safeGetDouble(item, "totalFilamentUsed", 0.0);
                            fileInfo.totalFilamentUsedLength = JsonUtils::safeGetDouble(item, "totalFilamentUsedLength", 0.0);
                            fileInfo.totalPrintTimes = JsonUtils::safeGetInt(item, "totalPrintTimes", 0);
                            fileInfo.lastPrintTime = JsonUtils::safeGetInt64(item, "lastPrintTime", 0);

                            if (item.contains("colorMap") && item["colorMap"].is_string())
                            {
                                nlohmann::json colorMapJson = nlohmann::json::parse(item["colorMap"].get<std::string>());
                                for (const auto &colorItem : colorMapJson)
                                {
                                    FilamentColorMapping colorMapping;
                                    colorMapping.color = JsonUtils::safeGetString(colorItem, "color", "");
                                    colorMapping.t = JsonUtils::safeGetInt(colorItem, "t", -1);
                                    colorMapping.type = JsonUtils::safeGetString(colorItem, "type", "");
                                    fileInfo.colorMapping.push_back(colorMapping);
                                }
                            }
                            fileListData.fileList.push_back(fileInfo);
                        }
                    }
                    GetFileListResult listResult;
                    listResult.data = std::move(fileListData);
                    return listResult;
                }
                else
                {
                    ELEGOO_LOG_ERROR("No data in file list response");
                    return GetFileListResult::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "No data in response");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to get file list, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse file list response: {}", e.what());
            return GetFileListResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    BizResult<std::string> HttpService::getThumbnailUrl(const std::string &thumbnailName)
    {
        if (thumbnailName.empty())
        {
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Thumbnail name cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get thumbnail URL");
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }
        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/oss/generate-pre-access-url?bucketAlias=iot-private&objectName=" + UrlUtils::UrlEncode(thumbnailName)));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get thumbnail URL: {}", result.message);
            return BizResult<std::string>::Error(result.code, result.message);
        }
        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];
                    std::string url = JsonUtils::safeGetString(data, "accessUrl", "");
                    return BizResult<std::string>::Ok(url);
                }
                else
                {
                    ELEGOO_LOG_ERROR("No data in thumbnail URL response");
                    return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "No data in response");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to get thumbnail URL, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse thumbnail URL response: {}", e.what());
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    GetFileDetailResult HttpService::getFileDetail(const GetFileDetailParams &params, bool needThumbnail)
    {
        if (params.printerId.empty())
        {
            return GetFileDetailResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        if (params.fileName.empty())
        {
            return GetFileDetailResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File name cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get file detail");
            return GetFileDetailResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }
        std::string serialNumber = getSerialNumberByPrinterId(params.printerId);
        if (serialNumber.empty())
        {
            ELEGOO_LOG_ERROR("Failed to get serial number for printer ID: {}", StringUtils::maskString(params.printerId));
            return GetFileDetailResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found");
        }
        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/local-file/filename?serialNo=" + UrlUtils::UrlEncode(serialNumber) + "&filename=" + UrlUtils::UrlEncode(params.fileName)));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get file detail: {}", result.message);
            return GetFileDetailResult::Error(result.code, result.message);
        }
        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);
            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];
                    FileDetail fileDetailData;
                    fileDetailData.fileName = JsonUtils::safeGetString(data, "filename", "");
                    fileDetailData.printTime = JsonUtils::safeGetInt64(data, "printTime", 0);
                    fileDetailData.layer = JsonUtils::safeGetInt(data, "layer", 0);
                    fileDetailData.layerHeight = JsonUtils::safeGetDouble(data, "layerHeight", 0.0);
                    fileDetailData.thumbnail = JsonUtils::safeGetString(data, "thumbnail", "");
                    fileDetailData.size = JsonUtils::safeGetInt64(data, "size", 0);
                    fileDetailData.createTime = JsonUtils::safeGetInt64(data, "createTime", 0);
                    fileDetailData.totalFilamentUsed = JsonUtils::safeGetDouble(data, "totalFilamentUsed", 0.0);
                    fileDetailData.totalFilamentUsedLength = JsonUtils::safeGetDouble(data, "totalFilamentUsedLength", 0.0);
                    fileDetailData.totalPrintTimes = JsonUtils::safeGetInt(data, "totalPrintTimes", 0);
                    fileDetailData.lastPrintTime = JsonUtils::safeGetInt64(data, "lastPrintTime", 0);

                    if (data.contains("colorMap") && data["colorMap"].is_string())
                    {
                        nlohmann::json colorMapJson = nlohmann::json::parse(data["colorMap"].get<std::string>());

                        for (const auto &colorItem : colorMapJson)
                        {
                            FilamentColorMapping colorMapping;
                            colorMapping.color = JsonUtils::safeGetString(colorItem, "color", "");
                            colorMapping.t = JsonUtils::safeGetInt(colorItem, "t", -1);
                            colorMapping.type = JsonUtils::safeGetString(colorItem, "type", "");
                            fileDetailData.colorMapping.push_back(colorMapping);
                        }
                    }

                    if (needThumbnail)
                    {
                        auto thumbnailResult = getThumbnailUrl(fileDetailData.thumbnail);
                        if (thumbnailResult.isSuccess())
                        {
                            fileDetailData.thumbnail = thumbnailResult.value();
                        }
                        else
                        {
                            ELEGOO_LOG_WARN("Failed to get thumbnail URL for {}: {}", fileDetailData.thumbnail, thumbnailResult.message);
                        }
                    }
                    GetFileDetailResult detailResult;
                    detailResult.data = std::move(fileDetailData);
                    return detailResult;
                }
                else
                {
                    ELEGOO_LOG_ERROR("No data in file detail response");
                    return GetFileDetailResult::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "No data in response");
                }
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to get file detail, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse file detail response: {}", e.what());
            return GetFileDetailResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    PrintTaskListResult HttpService::getPrintTaskList(const PrintTaskListParams &params)
    {
        if (params.printerId.empty())
        {
            return PrintTaskListResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get print task list");
            return PrintTaskListResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        std::string serialNumber = getSerialNumberByPrinterId(params.printerId);
        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/device/event-data/page?deviceCode=" + UrlUtils::UrlEncode(serialNumber) + "&pageNo=" + std::to_string(params.pageNumber) + "&pageSize=" + std::to_string(params.pageSize) + "&eventKey=history_task" + "&sort=create_time,desc"));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get print task list: {}", result.message);
            return PrintTaskListResult::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];
                    PrintTaskListData taskListData;
                    taskListData.totalTasks = JsonUtils::safeGetInt(data, "total", 0);
                    if (data.contains("list") && data["list"].is_array())
                    {
                        for (const auto &item : data["list"])
                        {
                            PrintTaskDetail taskDetail;
                            taskDetail.taskId = JsonUtils::safeGetString(item, "id", "");
                            if (item.contains("eventValue") && item["eventValue"].is_string())
                            {
                                std::string eventValueStr = item["eventValue"].get<std::string>();
                                try
                                {
                                    nlohmann::json eventValueJson = nlohmann::json::parse(eventValueStr);
                                    taskDetail.thumbnail = JsonUtils::safeGetString(eventValueJson, "thumbnail", "");
                                    taskDetail.taskName = JsonUtils::safeGetString(eventValueJson, "task_name", "");
                                    taskDetail.beginTime = JsonUtils::safeGetInt64(eventValueJson, "begin_time", 0);
                                    taskDetail.endTime = JsonUtils::safeGetInt64(eventValueJson, "end_time", 0);
                                    taskDetail.taskStatus = JsonUtils::safeGetInt(eventValueJson, "task_status", 0);
                                }
                                catch (const std::exception &e)
                                {
                                    ELEGOO_LOG_WARN("Failed to parse eventValue JSON for task {}: {}", taskDetail.taskId, e.what());
                                }
                            }
                            taskListData.taskList.push_back(taskDetail);
                        }
                    }

                    PrintTaskListResult listResult;
                    listResult.data = std::move(taskListData);
                    return listResult;
                }
                else
                {
                    ELEGOO_LOG_ERROR("No data in print task list response");
                    return PrintTaskListResult::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "No data in response");
                }
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to get print task list, code: {}", code);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse print task list response: {}", e.what());
            return PrintTaskListResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    DeletePrintTasksResult HttpService::deletePrintTasks(const DeletePrintTasksParams &params)
    {
        if (params.printerId.empty())
        {
            return DeletePrintTasksResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        if (params.taskIds.empty())
        {
            return DeletePrintTasksResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Task IDs cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot delete print tasks");
            return DeletePrintTasksResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }
        // nlohmann::json requestBody;
        // for (size_t i = 0; i < params.taskIds.size(); i++)
        // {
        //     requestBody["ids"].push_back(params.taskIds[i]);
        // }
        std::string urlParams = "?ids=";
        for (size_t i = 0; i < params.taskIds.size(); i++)
        {
            urlParams += params.taskIds[i];
            if (i != params.taskIds.size() - 1)
            {
                urlParams += ",";
            }
        }

        std::string serialNumber = getSerialNumberByPrinterId(params.printerId);
        urlParams += "&deviceCode=" + serialNumber;

        BizResult<HttpResponse> result = httpClient->del(buildUrlPath("/api/v1/device-management-server/device-data/event-data" + urlParams));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to delete print tasks: {}", result.message);
            return DeletePrintTasksResult::Error(result.code, result.message);
        }
        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        ELEGOO_LOG_INFO("Successfully deleted print tasks: {}", response.body);
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);
            if (code == 0)
            {
                ELEGOO_LOG_INFO("Print tasks deleted successfully");
                return DeletePrintTasksResult::Success();
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to delete print tasks, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse delete print tasks response: {}", e.what());
            return DeletePrintTasksResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    BizResult<nlohmann::json> HttpService::getPrinterStatus(const std::string &printerId)
    {
        if (printerId.empty())
        {
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get printer status");
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }
        std::string serialNumber = getSerialNumberByPrinterId(printerId);
        if (serialNumber.empty())
        {
            ELEGOO_LOG_WARN("Serial number not found for printer ID: {}", StringUtils::maskString(printerId));
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Serial number not found for printer ID");
        }
        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/device/report-data/list?deviceCode=" + UrlUtils::UrlEncode(serialNumber)));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get printer status: {}", result.message);
            return BizResult<nlohmann::json>::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        nlohmann::json resultJson;
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                {
                    nlohmann::json data = jsonResponse["data"];
                    for (auto it = data.begin(); it != data.end(); ++it)
                    {
                        const std::string &key = it.key();
                        if (it.value().is_array())
                        {
                            nlohmann::json subObject;
                            for (const auto &item : it.value())
                            {
                                std::string linkKey = JsonUtils::safeGetString(item, "reportLinkKey", "");
                                std::string reportValue = JsonUtils::safeGetString(item, "reportValue", "");
                                if (key == "external_device" && linkKey == "type")
                                {
                                    subObject[linkKey] = reportValue;
                                }
                                else if (!linkKey.empty())
                                {
                                    // Try to parse reportValue as JSON object, if fails then treat as string
                                    try
                                    {
                                        nlohmann::json valueJson = nlohmann::json::parse(reportValue);
                                        subObject[linkKey] = valueJson;
                                    }
                                    catch (...)
                                    {
                                        // Not valid JSON, try parsing as basic data type
                                        if (reportValue == "true")
                                            subObject[linkKey] = true;
                                        else if (reportValue == "false")
                                            subObject[linkKey] = false;
                                        else
                                        {
                                            // Try parsing as number
                                            try
                                            {
                                                // Check if it contains a decimal point, if so parse as floating point
                                                if (reportValue.find('.') != std::string::npos)
                                                {
                                                    double doubleValue = std::stod(reportValue);
                                                    subObject[linkKey] = doubleValue;
                                                }
                                                else
                                                {
                                                    // Try parsing as integer
                                                    long long intValue = std::stoll(reportValue);
                                                    subObject[linkKey] = intValue;
                                                }
                                            }
                                            catch (...)
                                            {
                                                // Not a number, treat as string
                                                subObject[linkKey] = reportValue;
                                            }
                                        }
                                    }
                                }
                            }
                            resultJson[key] = subObject;
                        }
                    }
                    return BizResult<nlohmann::json>::Ok(std::move(resultJson));
                }
                else
                {
                    ELEGOO_LOG_ERROR("No data in printer status response");
                    return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "No data in printer status response");
                }
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to get printer status, code: {}", code);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse printer status response: {}", e.what());
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse printer status response");
        }
    }

    BizResult<std::string> HttpService::uploadFile(const std::string &fileName, const std::string &filePath, std::function<bool(uint64_t current, uint64_t total)> progressCallback)
    {
        if (fileName.empty())
        {
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File name cannot be empty");
        }

        if (filePath.empty())
        {
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File path cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot upload file");
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        // Get file size to determine upload method
        if (!PathUtils::exists(filePath))
        {
            ELEGOO_LOG_ERROR("File not found: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "File not found");
        }

        uint64_t fileSize = PathUtils::fileSize(filePath);
        if (fileSize == static_cast<std::uintmax_t>(-1))
        {
            ELEGOO_LOG_ERROR("Failed to get file size: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "Failed to get file size");
        }

        if (fileSize == 0)
        {
            ELEGOO_LOG_ERROR("File is empty: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File is empty");
        }

        std::string fileMd5 = FileUtils::calculateMD5Base64(filePath);
        if (fileMd5.empty())
        {
            ELEGOO_LOG_ERROR("Failed to calculate MD5 for file: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to calculate file MD5");
        }

        // 500MB threshold
        constexpr uint64_t MULTIPART_THRESHOLD = 500ULL * 1024 * 1024;

        // Use multipart upload for files >= 500MB
        if (fileSize >= MULTIPART_THRESHOLD)
        {
            ELEGOO_LOG_INFO("File size {} bytes >= 500MB, using multipart upload", fileSize);
            return uploadFileMultipart(fileName, filePath, progressCallback);
        }

        // Use normal upload for files < 500MB
        ELEGOO_LOG_INFO("File size {} bytes < 500MB, using normal upload", fileSize);

        struct AliyunBucketInfo
        {
            std::string entrypoint;
            int64_t expireTime = 0;
            std::string accessUrl;
            std::string objectName;
            bool isPublicRead = false;
        };

        AliyunBucketInfo bucketInfo;

        // Get OSS bucket info
        {
            BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/oss/biz-entrypoint?filename=" + UrlUtils::UrlEncode(fileName) + "&bucketAlias=iot-private&module=gcode&fileMd5=" + fileMd5));
            if (!result.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to get file list: {}", result.message);
                return VoidResult::Error(result.code, result.message);
            }

            const auto &response = result.value();
            auto handleResult = handleResponse(response);
            if (!handleResult.isSuccess())
            {
                return handleResult;
            }
            ELEGOO_LOG_INFO("Successfully retrieved file list: {}", response.body);

            try
            {
                nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
                int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

                if (code == 0)
                {
                    if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                    {
                        nlohmann::json data = jsonResponse["data"];
                        bucketInfo.entrypoint = JsonUtils::safeGetString(data, "entrypoint", "");
                        bucketInfo.expireTime = JsonUtils::safeGetInt64(data, "expireTime", 0);
                        bucketInfo.accessUrl = JsonUtils::safeGetString(data, "accessUrl", "");
                        bucketInfo.objectName = JsonUtils::safeGetString(data, "objectName", "");
                        bucketInfo.isPublicRead = JsonUtils::safeGetBool(data, "isPublicRead", false);
                    }
                    else
                    {
                        ELEGOO_LOG_ERROR("No data in file list response");
                        return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "No data in response");
                    }
                }
                else
                {
                    std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                    ELEGOO_LOG_ERROR("Failed to get file list, code: {}, message: {}", code, msg);
                    return serverErrorToNetworkError(code);
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Failed to parse file list response: {}", e.what());
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
            }
        }

        // Upload file to OSS
        {
            if (bucketInfo.entrypoint.empty() || bucketInfo.accessUrl.empty() || bucketInfo.objectName.empty())
            {
                ELEGOO_LOG_ERROR("Invalid OSS bucket info, cannot upload file");
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Invalid OSS bucket info");
            }

            auto url = bucketInfo.entrypoint;
            HttpClient ossClient("");

            std::map<std::string, std::string> headers;
            headers["Content-Type"] = "application/octet-stream";
            headers["Content-MD5"] = fileMd5;

            BizResult<HttpResponse> uploadResult = ossClient.putFile(url, filePath, headers, progressCallback);
            if (!uploadResult.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to upload file to OSS: {}", uploadResult.message);
                return BizResult<std::string>::Error(uploadResult.code, uploadResult.message);
            }

            const auto &uploadResponse = uploadResult.value();
            if (uploadResponse.statusCode >= 200 && uploadResponse.statusCode < 300)
            {
                ELEGOO_LOG_INFO("File uploaded successfully to OSS");
                return BizResult<std::string>::Ok(bucketInfo.accessUrl);
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to upload file to OSS, status code: {}, response: {}", uploadResponse.statusCode, uploadResponse.body);
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_UNKNOWN_ERROR, StringUtils::formatErrorMessage("Failed to upload file to OSS.", uploadResponse.statusCode));
            }
        }
    }

    BizResult<std::string> HttpService::uploadFileMultipart(const std::string &fileName, const std::string &filePath, std::function<bool(uint64_t current, uint64_t total)> progressCallback, size_t partSize)
    {
        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot upload file");
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        // Get file size
        if (!PathUtils::exists(filePath))
        {
            ELEGOO_LOG_ERROR("File not found: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "File not found");
        }

        uint64_t fileSize = PathUtils::fileSize(filePath);
        if (fileSize == static_cast<std::uintmax_t>(-1))
        {
            ELEGOO_LOG_ERROR("Failed to get file size: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "Failed to get file size");
        }

        if (fileSize == 0)
        {
            ELEGOO_LOG_ERROR("File is empty: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File is empty");
        }

        // Calculate total parts
        int totalParts = static_cast<int>((fileSize + partSize - 1) / partSize);

        // Calculate MD5 for each part
        ELEGOO_LOG_INFO("Calculating MD5 for {} parts...", totalParts);
        std::vector<std::string> fileMd5List;
        {
            std::ifstream fileStream = PathUtils::openInputStream(filePath, std::ios::binary);
            if (!fileStream.is_open())
            {
                ELEGOO_LOG_ERROR("Failed to open file for MD5 calculation: {}", filePath);
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "Failed to open file");
            }

            for (int i = 0; i < totalParts; ++i)
            {
                uint64_t partOffset = static_cast<uint64_t>(i) * partSize;
                uint64_t currentPartSize = std::min(partSize, static_cast<size_t>(fileSize - partOffset));

                std::vector<char> partData(currentPartSize);
                fileStream.seekg(partOffset);
                fileStream.read(partData.data(), currentPartSize);
                std::string partMd5 = CryptoUtils::calculateMD5Base64(partData.data(), partData.size());
                fileMd5List.push_back(partMd5);
                ELEGOO_LOG_DEBUG("Part {} MD5: {}", i, partMd5);
            }
            fileStream.close();
        }

        // Step 1: Create multipart upload with all parameters
        std::string uploadId;
        std::string accessUrl;
        std::vector<std::pair<int, std::string>> uploadUrls; // part number -> predicateUrl
        {
            nlohmann::json requestBody;
            requestBody["bucketAlias"] = "iot-private";
            requestBody["module"] = "gcode";
            requestBody["filename"] = fileName;
            requestBody["partSize"] = totalParts;
            requestBody["isPermanentFile"] = false;
            requestBody["fileMd5List"] = fileMd5List;

            BizResult<HttpResponse> result = httpClient->post(
                buildUrlPath("/api/v1/device-management-server/oss/createMultipartUpload"),
                requestBody);

            if (!result.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to create multipart upload: {}", result.message);
                return BizResult<std::string>::Error(result.code, result.message);
            }

            const auto &response = result.value();
            auto handleResult = handleResponse(response);
            if (!handleResult.isSuccess())
            {
                return handleResult;
            }

            try
            {
                nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
                int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

                if (code == 0)
                {
                    if (jsonResponse.contains("data") && jsonResponse["data"].is_object())
                    {
                        nlohmann::json data = jsonResponse["data"];
                        uploadId = JsonUtils::safeGetString(data, "uploadId", "");
                        accessUrl = JsonUtils::safeGetString(data, "accessUrl", "");

                        if (uploadId.empty() || accessUrl.empty())
                        {
                            ELEGOO_LOG_ERROR("Invalid multipart upload response: missing uploadId or accessUrl");
                            return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "Invalid response");
                        }

                        // Parse multipartUploads array
                        if (data.contains("multipartUploads") && data["multipartUploads"].is_array())
                        {
                            for (const auto &upload : data["multipartUploads"])
                            {
                                int part = JsonUtils::safeGetInt(upload, "part", -1);
                                std::string predicateUrl = JsonUtils::safeGetString(upload, "predicateUrl", "");

                                if (part > 0 && !predicateUrl.empty())
                                {
                                    // part is 1-based index, convert to 0-based
                                    uploadUrls.emplace_back(part - 1, predicateUrl);
                                }
                            }

                            if (uploadUrls.size() != static_cast<size_t>(totalParts))
                            {
                                ELEGOO_LOG_ERROR("Mismatch in upload URLs count: expected {}, got {}", totalParts, uploadUrls.size());
                                return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "Invalid multipartUploads count");
                            }
                        }
                        else
                        {
                            ELEGOO_LOG_ERROR("No multipartUploads in response");
                            return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "No multipartUploads in response");
                        }
                    }
                    else
                    {
                        ELEGOO_LOG_ERROR("No data in multipart upload response");
                        return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "No data in response");
                    }
                }
                else
                {
                    std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                    ELEGOO_LOG_ERROR("Failed to create multipart upload, code: {}, message: {}", code, msg);
                    return serverErrorToNetworkError(code);
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Failed to parse multipart upload response: {}", e.what());
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
            }
        }

        // Step 2: Upload parts using predicateUrls
        std::vector<nlohmann::json> uploadedParts;
        uint64_t totalUploaded = 0;

        // Sort by part number to ensure correct order
        std::sort(uploadUrls.begin(), uploadUrls.end(),
                  [](const auto &a, const auto &b)
                  { return a.first < b.first; });

        // Open file once for all part uploads
        std::ifstream uploadFileStream = PathUtils::openInputStream(filePath, std::ios::binary);
        if (!uploadFileStream.is_open())
        {
            ELEGOO_LOG_ERROR("Failed to open file for part upload: {}", filePath);
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "Failed to open file");
        }

        for (const auto &[partNumber, predicateUrl] : uploadUrls)
        {
            // Upload this part
            uint64_t partOffset = static_cast<uint64_t>(partNumber) * partSize;
            uint64_t currentPartSize = std::min(partSize, static_cast<size_t>(fileSize - partOffset));

            // Read part data from the already opened file stream
            uploadFileStream.seekg(partOffset);
            std::vector<char> partData(currentPartSize);
            uploadFileStream.read(partData.data(), currentPartSize);

            if (!uploadFileStream.good() && !uploadFileStream.eof())
            {
                ELEGOO_LOG_ERROR("Failed to read part {} from file", partNumber);
                uploadFileStream.close();
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::FILE_TRANSFER_FAILED, "Failed to read file part");
            }

            HttpClient ossClient("");
            std::map<std::string, std::string> headers;
            headers["Content-Type"] = "application/octet-stream";
            headers["Content-MD5"] = fileMd5List[partNumber];

            BizResult<HttpResponse> uploadResult = ossClient.put(predicateUrl, partData, headers, std::nullopt,
                                                                 // Progress callback for this part
                                                                 [&, partNumber, currentPartSize](uint64_t uploaded, uint64_t total) -> bool
                                                                 {
                                                                     if (progressCallback)
                                                                     {
                                                                         uint64_t overallUploaded = totalUploaded + uploaded;
                                                                         return progressCallback(overallUploaded, fileSize);
                                                                     }
                                                                     return true;
                                                                 });
            if (!uploadResult.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to upload part {}: {}", partNumber, uploadResult.message);
                uploadFileStream.close();
                return BizResult<std::string>::Error(uploadResult.code, uploadResult.message);
            }

            const auto &uploadResponse = uploadResult.value();
            if (uploadResponse.statusCode < 200 || uploadResponse.statusCode >= 300)
            {
                ELEGOO_LOG_ERROR("Failed to upload part {}, status code: {}", partNumber, uploadResponse.statusCode);
                uploadFileStream.close();
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::SERVER_UNKNOWN_ERROR, "Failed to upload part");
            }
            // Update progress
            totalUploaded += currentPartSize;
            if (progressCallback)
            {
                if (!progressCallback(totalUploaded, fileSize))
                {
                    ELEGOO_LOG_WARN("Upload cancelled by user");
                    uploadFileStream.close();
                    return BizResult<std::string>::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Upload cancelled");
                }
            }

            ELEGOO_LOG_INFO("Uploaded part {}/{}, size: {}", partNumber + 1, totalParts, currentPartSize);
        }

        // Close file stream after all uploads
        uploadFileStream.close();

        // Step 3: Complete multipart upload
        {
            BizResult<HttpResponse> result = httpClient->post(
                buildUrlPath("/api/v1/device-management-server/oss/completeMultipartUpload?uploadId=" + UrlUtils::UrlEncode(uploadId)), nlohmann::json());

            if (!result.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to complete multipart upload: {}", result.message);
                return BizResult<std::string>::Error(result.code, result.message);
            }

            const auto &response = result.value();
            auto handleResult = handleResponse(response);
            if (!handleResult.isSuccess())
            {
                return handleResult;
            }

            try
            {
                nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
                int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

                if (code == 0)
                {
                    if (jsonResponse.contains("data") && jsonResponse["data"].is_string())
                    {
                        accessUrl = jsonResponse["data"].get<std::string>();
                    }
                    ELEGOO_LOG_INFO("Multipart upload completed successfully: {}", accessUrl);
                    return BizResult<std::string>::Ok(accessUrl);
                }
                else
                {
                    std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                    ELEGOO_LOG_ERROR("Failed to complete multipart upload, code: {}, message: {}", code, msg);
                    return serverErrorToNetworkError(code);
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Failed to parse complete multipart upload response: {}", e.what());
                return BizResult<std::string>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
            }
        }
    }

    VoidResult HttpService::updatePrinterName(const UpdatePrinterNameParams &params)
    {
        if (params.printerId.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        if (params.printerName.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer name cannot be empty");
        }

        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot update printer name");
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }
        std::string serialNumber = getSerialNumberByPrinterId(params.printerId);
        if (serialNumber.empty())
        {
            ELEGOO_LOG_WARN("Cannot find serial number for printer id: {}, cannot update printer name", StringUtils::maskString(params.printerId));
            return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Invalid printer ID");
        }
        nlohmann::json requestBody;
        requestBody["serialNo"] = serialNumber;
        requestBody["deviceName"] = params.printerName;

        BizResult<HttpResponse> result = httpClient->put(buildUrlPath("/api/v1/device-management-server/device/name"), requestBody);
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to update printer name: {}", result.message);
            return VoidResult::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return handleResult;
        }
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                ELEGOO_LOG_INFO("Printer name updated successfully");
                return VoidResult::Success();
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "message", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to update printer name, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse update printer name response: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    GetLicenseExpiredDevicesResult HttpService::getLicenseExpiredDevices()
    {
        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot get license expired devices");
            return GetLicenseExpiredDevicesResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        BizResult<HttpResponse> result = httpClient->get(buildUrlPath("/api/v1/device-management-server/device/agora-license/list/expire"));
        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to get license expired devices: {}", result.message);
            return GetLicenseExpiredDevicesResult::Error(result.code, result.message);
        }

        const auto &response = result.value();
        auto handleResult = handleResponse(response);
        if (!handleResult.isSuccess())
        {
            return GetLicenseExpiredDevicesResult::Error(handleResult.code, handleResult.message);
        }

        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                GetLicenseExpiredDevicesData data;
                if (jsonResponse.contains("data") && jsonResponse["data"].is_array())
                {
                    for (const auto &item : jsonResponse["data"])
                    {
                        LicenseExpiredDevice device;
                        device.serialNumber = JsonUtils::safeGetString(item, "serialNo", "");
                        device.status = JsonUtils::safeGetInt(item, "status", 0);
                        data.devices.push_back(device);
                    }
                }
                return GetLicenseExpiredDevicesResult::Ok(std::move(data));
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "msg", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to get license expired devices, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse license expired devices response: {}", e.what());
            return GetLicenseExpiredDevicesResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to parse response");
        }
    }

    RenewLicenseResult HttpService::renewLicense(const RenewLicenseParams &params)
    {
        auto httpClient = getHttpClient();
        if (!httpClient)
        {
            ELEGOO_LOG_WARN("HTTP client not initialized, cannot renew license");
            return RenewLicenseResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP client not initialized");
        }

        if (params.serialNumber.empty())
        {
            ELEGOO_LOG_ERROR("Serial number is required for license renewal");
            return RenewLicenseResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Serial number is required");
        }

        try
        {
            nlohmann::json requestBody;
            requestBody["serialNo"] = params.serialNumber;

            BizResult<HttpResponse> result = httpClient->post(
                buildUrlPath("/api/v1/device-management-server/device/agora-license/renew"),
                requestBody.dump(),
                {{"Content-Type", "application/json"}});

            if (!result.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to renew license: {}", result.message);
                return RenewLicenseResult::Error(result.code, result.message);
            }

            const auto &response = result.value();
            auto handleResult = handleResponse(response);
            if (!handleResult.isSuccess())
            {
                return handleResult;
            }

            nlohmann::json jsonResponse = nlohmann::json::parse(response.body);
            int code = JsonUtils::safeGetInt(jsonResponse, "code", -1);

            if (code == 0)
            {
                ELEGOO_LOG_INFO("License renewed successfully for device: {}", params.serialNumber);
                return RenewLicenseResult::Success();
            }
            else
            {
                std::string msg = JsonUtils::safeGetString(jsonResponse, "msg", "Unknown error");
                ELEGOO_LOG_ERROR("Failed to renew license, code: {}, message: {}", code, msg);
                return serverErrorToNetworkError(code);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to renew license: {}", e.what());
            return RenewLicenseResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to renew license");
        }
    }
}
