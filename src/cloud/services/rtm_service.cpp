#include "services/rtm_service.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/json_utils.h"
#include <nlohmann/json.hpp>
#include "private_config.h"
#include "IAgoraRtmClient.h"
#include "types/internal/internal.h"
#include "types/internal/json_serializer.h"
#include <future>
namespace elink
{
    RtmService::RtmService()
    {
    }

    RtmService::~RtmService()
    {
        cleanup();
    }

    VoidResult RtmService::initialize()
    {
        if (m_initialized.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_IN_PROGRESS, "RTM service is already initialized");
        }

        try
        {
            auto result = initializeClient();
            if (!result.isSuccess())
            {
                return result;
            }

            m_initialized.store(true);
            ELEGOO_LOG_INFO("RTM service initialization completed");
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "RTM service initialization failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void RtmService::cleanup()
    {
        if (!m_initialized.load())
        {
            return;
        }

        try
        {
            cleanupClient();

            // Clean up different data structures using corresponding locks
            {
                std::lock_guard<std::mutex> lock(m_eventCallbackMutex);
                m_eventCallback = nullptr;
            }

            {
                std::lock_guard<std::mutex> lock(m_printersMutex);
                m_printers.clear();
                m_messageAdapters.clear();
            }

            m_initialized.store(false);
            ELEGOO_LOG_INFO("RTM service cleanup completed");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred during RTM service cleanup: {}", e.what());
        }
    }

    bool RtmService::isInitialized() const
    {
        return m_initialized.load();
    }

    VoidResult RtmService::connect(const AgoraCredential &credential)
    {
        // Check if there are valid Agora credentials
        if (credential.rtmUserId.empty() || credential.rtmToken.empty())
        {
            ELEGOO_LOG_WARN("Cannot connect RTM: missing user ID or RTM token");
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Missing user ID or RTM token");
        }

        try
        {
            // Save credential information for subsequent use
            {
                std::lock_guard<std::mutex> lock(m_credentialMutex);
                m_cachedCredential = credential;
            }

            // Create new RTM configuration
            RtmConfig config;
            config.appId = AGORA_APP_ID;
            config.userId = credential.rtmUserId;
            config.token = credential.rtmToken;

            // RTM client operations require locking
            {
                std::lock_guard<std::mutex> lock(m_clientMutex);

                // If RTM client already exists and user ID matches, try updating token first
                if (m_rtmClient)
                {
                    // Different user, need to update configuration
                    VoidResult updateResult = m_rtmClient->updateConfig(config);
                    if (updateResult.isSuccess())
                    {
                        ELEGOO_LOG_INFO("RTM client updated config for user: {}", StringUtils::maskString(credential.rtmUserId));
                        // return VoidResult::Success();
                    }
                    else
                    {
                        ELEGOO_LOG_WARN("RTM config update failed, will recreate client: {}", updateResult.message);
                        m_rtmClient.reset();
                    }
                }

                // Create or recreate RTM client
                if (!m_rtmClient)
                {
                    m_rtmClient = createRtmClient(config);
                    if (!m_rtmClient)
                    {
                        ELEGOO_LOG_ERROR("Failed to create RTM client");
                        return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to create RTM client");
                    }

                    setupCallbacks();
                    ELEGOO_LOG_INFO("RTM client callbacks configured successfully");
                }

                // Login to RTM
                if (!m_rtmClient->isOnline())
                {
                    VoidResult loginResult = m_rtmClient->login(credential.rtmToken);
                    if (loginResult.isSuccess())
                    {
                        ELEGOO_LOG_INFO("RTM client logged in successfully for user: {} with process mutex protection", StringUtils::maskString(credential.rtmUserId));
                        subscribeToChannels(credential);
                        return VoidResult::Success();
                    }
                    else
                    {
                        ELEGOO_LOG_ERROR("RTM login failed: {}", loginResult.message);
                        return loginResult;
                    }
                }
            }

            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "RTM connection failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void RtmService::disconnect()
    {
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            if (m_rtmClient)
            {
                try
                {
                    m_rtmClient->logout();
                    ELEGOO_LOG_INFO("RTM client logged out");
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_ERROR("Error occurred while logging out RTM client: {}", e.what());
                }
            }
        }

        // Clear cached credentials
        {
            std::lock_guard<std::mutex> lock(m_credentialMutex);
            m_cachedCredential = AgoraCredential{};
        }
    }

    bool RtmService::isConnected() const
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        return m_rtmClient && m_rtmClient->isOnline();
    }

    bool RtmService::isLoginOtherDevice() const
    {
        return m_isLoginOtherDevice.load();
    }

    void RtmService::clearLoginOtherDeviceState()
    {
        if (m_isLoginOtherDevice.load())
        {
            ELEGOO_LOG_INFO("Clearing login from other device state, allowing reconnection");
            m_isLoginOtherDevice.store(false);
        }
    }

    VoidResult RtmService::sendMessage(const SendRtmMessageParams &params)
    {
        // First check client status
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            if (!m_rtmClient || !m_rtmClient->isLoggedIn())
            {
                ELEGOO_LOG_WARN("RTM client not initialized or not logged in, cannot send message");
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized or not logged in");
            }
        }

        std::string serialNumber = getSerialNumberByPrinterId(params.printerId);
        if (serialNumber.empty())
        {
            ELEGOO_LOG_WARN("No serial number found for printerId: {}, cannot send RTM message", StringUtils::maskString(params.printerId));
            return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found or adapter not available");
        }

        // Get cached credentials
        std::string channelName;
        {
            std::lock_guard<std::mutex> lock(m_credentialMutex);
            if (m_cachedCredential.rtmUserId.empty())
            {
                ELEGOO_LOG_WARN("No cached credential available for RTM message");
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "No cached credential available");
            }
            channelName = m_cachedCredential.userId + serialNumber;
        }

        try
        {
            VoidResult result;
            {
                std::lock_guard<std::mutex> lock(m_clientMutex);
                result = m_rtmClient->publish(channelName, params.message);
            }

            if (result.isSuccess())
            {
                ELEGOO_LOG_INFO("RTM message sent successfully to printer: {}", StringUtils::maskString(params.printerId));
                return VoidResult::Success();
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to send RTM message: {}", result.message);
                return result;
            }
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "Exception while sending RTM message: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    BizResult<nlohmann::json> RtmService::sendRequest(const BizRequest &request,
                                                  std::chrono::milliseconds timeout)
    {
        // Mask sensitive info in params for logging
        nlohmann::json maskedParams = request.params;
        if (maskedParams.contains("printerId") && maskedParams["printerId"].is_string())
        {
            maskedParams["printerId"] = StringUtils::maskString(maskedParams["printerId"].get<std::string>());
        }
        ELEGOO_LOG_DEBUG("RTM Request details: method={}, params={}", static_cast<int>(request.method), maskedParams.dump());

        if (!isConnected())
        {
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::SERVER_RTM_NOT_CONNECTED, "RTM client not connected or not logged in"};
        }

        // Get target printer ID
        std::string printerId;
        if (request.params.contains("printerId") && request.params["printerId"].is_string())
        {
            printerId = request.params["printerId"];
        }
        else
        {
            ELEGOO_LOG_ERROR("Missing printerId in RTM request parameters");
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::INVALID_PARAMETER, "Missing printerId parameter"};
        }

        std::string serialNumber = getSerialNumberByPrinterId(printerId);
        if (serialNumber.empty())
        {
            ELEGOO_LOG_ERROR("No serial number found for printerId: {}", StringUtils::maskString(printerId));
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found or adapter not available"};
        }

        // Find corresponding message adapter
        auto adapterIt = m_messageAdapters.find(printerId);
        if (adapterIt == m_messageAdapters.end())
        {
            ELEGOO_LOG_ERROR("No message adapter found for printer: {}", StringUtils::maskString(printerId));
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found or adapter not available"};
        }

        auto &adapter = adapterIt->second;

        // Use adapter to convert standard request to printer-specific format
        auto printerBizRequest = adapter->convertRequest(request.method, request.params, timeout);
        if (!printerBizRequest.isValid())
        {
            ELEGOO_LOG_ERROR("Failed to convert request using adapter: {}", printerBizRequest.message);
            return BizResult<nlohmann::json>{
                printerBizRequest.code, printerBizRequest.message};
        }

        // Use promise/future for synchronous blocking
        auto promise = std::make_shared<std::promise<BizResult<nlohmann::json>>>();
        std::future<BizResult<nlohmann::json>> future = promise->get_future();

        // Set actual timeout value
        std::chrono::milliseconds actualTimeout = timeout.count() > 0
                                                      ? timeout
                                                      : std::chrono::milliseconds(10000); // Default 10 seconds

        // Register pending request with promise
        {
            std::lock_guard<std::mutex> lock(requestsMutex_);
            pendingRequests_[printerBizRequest.requestId] = {
                printerBizRequest.requestId,
                promise,
                request.method};
        }

        // Send message via RTM
        try
        {
            VoidResult result;
            std::string channelName;

            // Get channel name and send message
            {
                std::lock_guard<std::mutex> credLock(m_credentialMutex);
                channelName = m_cachedCredential.userId + serialNumber;
            }

            {
                std::lock_guard<std::mutex> clientLock(m_clientMutex);
                result = m_rtmClient->publish(channelName, printerBizRequest.data);
            }

            if (!result.isSuccess())
            {
                // If sending fails, remove registered request
                {
                    std::lock_guard<std::mutex> lock(requestsMutex_);
                    pendingRequests_.erase(printerBizRequest.requestId);
                }
                ELEGOO_LOG_ERROR("Failed to send RTM request: {}", result.message);
                return BizResult<nlohmann::json>{result.code, result.message};
            }

            // Wait for response with timeout
            if (future.wait_for(actualTimeout) == std::future_status::timeout)
            {
                // Timeout - remove pending request
                {
                    std::lock_guard<std::mutex> lock(requestsMutex_);
                    pendingRequests_.erase(printerBizRequest.requestId);
                }
                ELEGOO_LOG_ERROR("RTM request timeout after {}ms", actualTimeout.count());
                return BizResult<nlohmann::json>{
                    ELINK_ERROR_CODE::OPERATION_TIMEOUT, "Request timeout"};
            }

            // Get result from future
            return future.get();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "Exception while sending RTM request: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg};
        }
    }

    void RtmService::handleResponseMessage(const std::string &requestId, ELINK_ERROR_CODE code, std::string message, const std::optional<nlohmann::json> &result)
    {
        if (!requestId.empty())
        {
            std::shared_ptr<std::promise<BizResult<nlohmann::json>>> promise;
            {
                std::lock_guard<std::mutex> lock(requestsMutex_);
                auto it = pendingRequests_.find(requestId);
                if (it != pendingRequests_.end())
                {
                    promise = it->second.promise;
                    // Remove processed request
                    pendingRequests_.erase(it);
                }
                else
                {
                    ELEGOO_LOG_WARN("Received response for unknown request ID: {}", StringUtils::maskString(requestId));
                    return;
                }
            }

            // Set promise value outside the lock to avoid potential deadlock
            if (promise)
            {
                try
                {
                    BizResult<nlohmann::json> res;
                    res.code = code;
                    res.data = result;
                    res.message = message;
                    promise->set_value(res);
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_ERROR("Exception while setting promise value: {}", e.what());
                }
            }
        }
        else
        {
            ELEGOO_LOG_WARN("Received response without request ID");
        }
    }

    void RtmService::handleEventMessage(const BizEvent &event)
    {
        EventCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_eventCallbackMutex);
            callback = m_eventCallback;
        }
        if (callback)
        {
            callback(event);
        }
    }
    void RtmService::setEventCallback(EventCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_eventCallbackMutex);
        m_eventCallback = callback;
    }

    void RtmService::setConnectionStateCallback(ConnectionStateCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_connectionStateCallbackMutex);
        m_connectionStateCallback = callback;
    }

    void RtmService::updatePrinters(const std::vector<PrinterInfo> &printers)
    {
        std::lock_guard<std::mutex> lock(m_printersMutex);
        m_printers = printers;
    }

    std::shared_ptr<IMessageAdapter> RtmService::getMessageAdapter(const std::string &printerId) const
    {
        std::lock_guard<std::mutex> lock(m_printersMutex);

        auto it = m_messageAdapters.find(printerId);
        if (it != m_messageAdapters.end())
        {
            return it->second;
        }

        ELEGOO_LOG_WARN("Message adapter not found for printer: {}", StringUtils::maskString(printerId));
        return nullptr;
    }

    void RtmService::setMessageAdapter(const std::string &printerId, std::shared_ptr<IMessageAdapter> adapter)
    {
        std::lock_guard<std::mutex> lock(m_printersMutex);

        if (adapter)
        {
            m_messageAdapters[printerId] = adapter;
            ELEGOO_LOG_INFO("Message adapter set for printer: {}", StringUtils::maskString(printerId));
        }
        else
        {
            auto it = m_messageAdapters.find(printerId);
            if (it != m_messageAdapters.end())
            {
                m_messageAdapters.erase(it);
                ELEGOO_LOG_INFO("Message adapter removed for printer: {}", StringUtils::maskString(printerId));
            }
        }
    }

    VoidResult RtmService::initializeClient()
    {
        try
        {
            // RTM client will be lazily initialized, created during connect
            ELEGOO_LOG_INFO("RTM client prepared for lazy initialization");
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "RTM preparation failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void RtmService::cleanupClient()
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        if (m_rtmClient)
        {
            try
            {
                m_rtmClient->logout();
                m_rtmClient.reset();
                ELEGOO_LOG_INFO("RTM client cleaned up");
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Error occurred while cleaning up RTM client: {}", e.what());
            }
        }
    }

    void RtmService::setupCallbacks()
    {
        if (!m_rtmClient)
        {
            return;
        }

        // Set message callback
        m_rtmClient->setMessageCallback([this](const RtmMessage &message)
                                        { onRtmMessageReceived(message); });

        m_rtmClient->setConnectionStateCallback([this](RtmConnectionState state, RtmConnectionChangeReason reason)
                                                {
            ELEGOO_LOG_INFO("RTM connection state changed: state={}, reason={}", static_cast<int>(state), static_cast<int>(reason));
            
            // Check if connected
            bool isConnected = (state == agora::rtm::RTM_CONNECTION_STATE::RTM_CONNECTION_STATE_CONNECTED);
            
            // Trigger connection state callback
            {
                std::lock_guard<std::mutex> lock(m_connectionStateCallbackMutex);
                if (m_connectionStateCallback)
                {
                    m_connectionStateCallback(isConnected, state, reason);
                }
            }
            
            // Handle same UID login case
            if (state == agora::rtm::RTM_CONNECTION_STATE::RTM_CONNECTION_STATE_FAILED && 
                reason == agora::rtm::RTM_CONNECTION_CHANGE_REASON::RTM_CONNECTION_CHANGED_SAME_UID_LOGIN)
            {
                ELEGOO_LOG_WARN("RTM connection failed due to same UID login from another device");
                m_isLoginOtherDevice.store(true);
                BizEvent event;
                event.method = MethodType::ON_LOGGED_IN_ELSEWHERE;
                handleEventMessage(event);
            } });
    }

    void RtmService::onRtmMessageReceived(const RtmMessage &message)
    {
        try
        {
            ELEGOO_LOG_DEBUG("Received RTM message: channel={}, publisher={}, content={}",
                             message.channelName, StringUtils::maskString(message.publisher), message.content);

            std::string userIdStr;
            {
                std::lock_guard<std::mutex> lock(m_credentialMutex);
                userIdStr = m_cachedCredential.userId;
            }

            std::string serialNumber = "";
            {
                std::lock_guard<std::mutex> lock(m_printersMutex);
                for (const auto &printer : m_printers)
                {
                    if (message.publisher == userIdStr + printer.serialNumber)
                    {
                        serialNumber = printer.serialNumber;
                        break;
                    }
                }
            }

            if (serialNumber.empty())
            {
                ELEGOO_LOG_WARN("Received RTM message from unknown publisher: {}", StringUtils::maskString(message.publisher));
                return;
            }

            std::string printerId = getPrinterIdBySerialNumber(serialNumber);

            try
            {
                nlohmann::json messageJson = nlohmann::json::parse(message.content, nullptr, false);
                if (messageJson.is_discarded())
                {
                    ELEGOO_LOG_WARN("Failed to parse RTM message content as JSON: {}", message.content);
                    return;
                }

                if (messageJson.contains("method") && messageJson["method"].is_number())
                {
                    // Indicates download file progress message
                    if (messageJson["method"].get<int>() == 6006)
                    {
                        if (!messageJson.contains("result") || !messageJson["result"].is_object())
                        {
                            ELEGOO_LOG_WARN("Invalid download file status message format: {}", message.content);
                            return;
                        }
                        nlohmann::json result = messageJson["result"];
                        DownloadFileStatus downloadFileStatus;
                        downloadFileStatus.printerId = printerId;
                        downloadFileStatus.taskId = JsonUtils::safeGetString(result, "taskID", "");
                        downloadFileStatus.progress = JsonUtils::safeGetInt(result, "progress", 0);
                        downloadFileStatus.status = JsonUtils::safeGetInt(result, "status", 0);

                        downloadFileStatus.lastUpdatedTime = std::chrono::steady_clock::now();
                        {
                            std::lock_guard<std::mutex> lock(m_downloadFileStatusMutex);

                            m_cacheDownloadFileStatus[printerId] = downloadFileStatus;
                        }
                        return;
                    }
                }
            }
            catch (...)
            {
                ELEGOO_LOG_WARN("Failed to parse RTM message content as JSON: {}", message.content);
                return;
            }

            BizEvent rtmEvent;
            rtmEvent.method = MethodType::ON_RTM_MESSAGE;
            RtmMessageData rtmEventData;
            rtmEventData.printerId = printerId;
            rtmEventData.message = message.content;
            rtmEvent.data = rtmEventData;
            handleEventMessage(rtmEvent);

            std::shared_ptr<IMessageAdapter> adapter;
            {
                std::lock_guard<std::mutex> lock(m_printersMutex);
                auto it = m_messageAdapters.find(rtmEventData.printerId);
                if (it != m_messageAdapters.end())
                {
                    adapter = it->second;
                }
            }

            if (adapter)
            {
                std::vector<std::string> parsedMessageTypes = adapter->parseMessageType(message.content);
                if (parsedMessageTypes.empty())
                {
                    ELEGOO_LOG_ERROR("Failed to parse message type for printer {}: {}", StringUtils::maskString(rtmEventData.printerId), message.content);
                    return;
                }
                for (size_t i = 0; i < parsedMessageTypes.size(); i++)
                {
                    const std::string &parsedMessageType = parsedMessageTypes[i];
                    if (parsedMessageType == "response")
                    {
                        // Convert printer response to standard response format
                        PrinterBizResponse standardResponse = adapter->convertToResponse(message.content);
                        if (!standardResponse.isValid())
                        {
                            std::string maskedContent = message.content;
                            if (!serialNumber.empty() && maskedContent.find(serialNumber) != std::string::npos)
                            {
                                std::string maskSn = StringUtils::maskString(serialNumber);
                                maskedContent = StringUtils::replaceAll(maskedContent, serialNumber, maskSn);
                            }

                            ELEGOO_LOG_WARN("Invalid response message for printer {}: {}", StringUtils::maskString(rtmEventData.printerId), maskedContent);
                            continue;
                        }
                        handleResponseMessage(standardResponse.requestId, standardResponse.code, standardResponse.message, standardResponse.data);
                    }
                    else if (parsedMessageType == "event")
                    {
                        // Convert printer event to standard event format
                        PrinterBizEvent data = adapter->convertToEvent(message.content);
                        if (data.isValid())
                        {
                            BizEvent bizEvent;
                            bizEvent.method = data.method;
                            bizEvent.data = data.data.value();
                            ELEGOO_LOG_DEBUG("Received event from printer {}, method={}",
                                             StringUtils::maskString(rtmEventData.printerId), (int)data.method);
                            handleEventMessage(bizEvent);
                        }
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while handling RTM message callback: {}", e.what());
        }
    }

    void RtmService::subscribeToChannels(const AgoraCredential &credential)
    {
        // std::lock_guard<std::mutex> lock(m_clientMutex);
        if (!m_rtmClient)
        {
            return;
        }

        try
        {
            // Subscribe to user channel
            m_rtmClient->subscribe(credential.userId);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while subscribing to RTM channels: {}", e.what());
        }
    }

    std::unique_ptr<RtmClient> RtmService::createRtmClient(const RtmConfig &config)
    {
        try
        {
            // Here we need to implement specific RTM client creation logic
            // Since the original code doesn't show the specific implementation of createRtmClient, we use a placeholder here
            // The actual implementation needs to be completed according to the specific RTM client library
            return std::make_unique<RtmClient>(config);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to create RTM client: {}", e.what());
            return nullptr;
        }
    }

}
