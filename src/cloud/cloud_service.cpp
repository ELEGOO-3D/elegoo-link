#include "cloud_service.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "app_utils.h"
#include "types/internal/internal.h"
#include "types/internal/json_serializer.h"

// Macro to validate printer ID and RTM service state
#define VALIDATE_PRINTER_AND_RTM_SERVICE(ReturnType)                                                      \
    if (params.printerId.empty())                                                                         \
    {                                                                                                     \
        return ReturnType::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");      \
    }                                                                                                     \
    {                                                                                                     \
        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);                                        \
        if (!m_initialized.load())                                                                        \
        {                                                                                                 \
            return ReturnType::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "Cloud service not initialized"); \
        }                                                                                                 \
        if (!m_rtmService)                                                                                \
        {                                                                                                 \
            return ReturnType::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM service not initialized");   \
        }                                                                                                 \
    }

namespace elink
{
    static std::string s_cloudStaticWebPath;
    std::string cloudStaticWebPath()
    {
        return s_cloudStaticWebPath;
    }
    CloudService::CloudService()
    {
    }

    CloudService::~CloudService()
    {
        cleanup();
    }

    void CloudService::setEventCallback(EventCallback callback)
    {
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_eventCallback = callback;
        }

        // Pass the callback to child services
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            if (m_mqttService)
            {
                m_mqttService->setEventCallback(callback);
            }
            if (m_rtmService)
            {
                m_rtmService->setEventCallback(callback);

                // Set RTM connection state callback
                m_rtmService->setConnectionStateCallback([this](bool isConnected, RtmConnectionState state, RtmConnectionChangeReason reason)
                                                         {
                    if(isConnected)
                    {
                        // Update online status based on connection state
                        setOnlineStatus(isConnected);
                    } });
            }
        }
    }

    VoidResult CloudService::initialize(const CloudService::NetworkConfig &config)
    {
        if (m_initialized.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_IN_PROGRESS, "CloudService is already initialized");
        }

        try
        {
            s_cloudStaticWebPath = config.staticWebPath;
            {
                // m_httpService(std::make_unique<HttpService>()), m_mqttService(std::make_unique<MqttService>()), m_rtmService(std::make_unique<RtmService>())
                std::lock_guard<std::shared_mutex> servicesLock(m_servicesMutex);
                m_httpService = std::make_unique<HttpService>();
                m_mqttService = std::make_unique<MqttService>();
                m_rtmService = std::make_unique<RtmService>();
            }
            // Update configuration
            {
                std::lock_guard<std::mutex> configLock(m_configMutex);
                m_networkConfig = config;
            }

            // Initialize services
            std::lock_guard<std::shared_mutex> servicesLock(m_servicesMutex);

            // Initialize HTTP service
            auto httpResult = m_httpService->initialize(m_networkConfig.region, m_networkConfig.userAgent, m_networkConfig.baseApiUrl, config.caCertPath);
            if (!httpResult.isSuccess())
            {
                ELEGOO_LOG_ERROR("HTTP service initialization failed: {}", httpResult.message);
                return httpResult;
            }

            // Initialize MQTT service
            auto mqttResult = m_mqttService->initialize(config.caCertPath);
            if (!mqttResult.isSuccess())
            {
                ELEGOO_LOG_ERROR("MQTT service initialization failed: {}", mqttResult.message);
                // Continue initialization, MQTT failure does not prevent overall initialization
            }

            // Initialize RTM service
            auto rtmResult = m_rtmService->initialize();
            if (!rtmResult.isSuccess())
            {
                ELEGOO_LOG_ERROR("RTM service initialization failed: {}", rtmResult.message);
                // Continue initialization, RTM failure does not prevent overall initialization
            }

            m_initialized.store(true);
            ELEGOO_LOG_INFO("CloudService initialization completed");
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "CloudService initialization failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void CloudService::cleanup()
    {
        if (!m_initialized.load())
        {
            return;
        }

        try
        {
            // First set the status to uninitialized to prevent new operations
            m_initialized.store(false);
            {
                std::lock_guard<std::shared_mutex> servicesLock(m_servicesMutex);
                if (m_httpService)
                {
                    m_httpService->cleanup();
                    m_httpService.reset();
                }
            }
            // Stop background tasks
            stopBackgroundTasks();

            setEventCallback(nullptr);
            // Clean up each service
            {

                if (m_mqttService)
                {
                    m_mqttService->cleanup();
                    m_mqttService.reset();
                }

                if (m_rtmService)
                {
                    m_rtmService->cleanup();
                    m_rtmService.reset();
                }
            }

            // Clean up other resources
            {
                std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
                m_eventCallback = nullptr;
            }

            {
                std::lock_guard<std::shared_mutex> printersLock(m_printersMutex);
                m_printers.clear();
                m_messageAdapters.clear();
            }

            // Clean up credentials
            {
                std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
                m_agoraCredential = nullptr;
                m_mqttCredential = nullptr;
            }

            // Clean up upload status
            {
                std::lock_guard<std::mutex> uploadLock(m_uploadingFilesMutex);
                m_uploadingFiles.clear();
            }

            ELEGOO_LOG_INFO("CloudService cleanup completed");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred during CloudService cleanup: {}", e.what());
        }
    }

    bool CloudService::isInitialized() const
    {
        return m_initialized.load();
    }

    VoidResult CloudService::setRegion(const SetRegionParams &params)
    {
        {
            std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);

            // Check if the new region matches the cached region
            if (m_cachedRegionParams == params)
            {
                return VoidResult::Success();
            }
        }
        clearHttpCredential();
        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
        if (!m_httpService)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
        }
        auto result = m_httpService->setRegion(params);
        if (!result.isSuccess())
        {
            return result;
        }

        if (!m_mqttService)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "MQTT service not initialized");
        }
        m_mqttService->setCaCertPath(params.caCertPath);

        {
            std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
            // Update the cached region
            m_cachedRegionParams = params;
        }
        return result;
    }

    GetUserInfoResult CloudService::getUserInfo(const GetUserInfoParams &params)
    {
        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
        GetUserInfoResult result;
        if (!m_httpService)
        {
            return GetUserInfoResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
        }
        result = m_httpService->getUserInfo();
        return result;
    }

    VoidResult CloudService::setHttpCredential(const HttpCredential &credential)
    {
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            if (!m_httpService)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
            }

            {
                std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
                // Check if the new credential matches the cached credential
                if (m_cachedHttpCredential.userId == credential.userId &&
                    m_cachedHttpCredential.accessToken == credential.accessToken &&
                    m_cachedHttpCredential.refreshToken == credential.refreshToken)
                {
                    return VoidResult::Success();
                }
            }

            auto result = m_httpService->setCredential(credential);
            if (!result.isSuccess())
            {
                return result;
            }

            // Clear login from other device state to allow reconnection with new credentials
            if (m_rtmService)
            {
                m_rtmService->clearLoginOtherDeviceState();
            }
        }

        m_lastHttpErrorCode = ELINK_ERROR_CODE::SUCCESS;

        // Update the cached credential
        {
            std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
            m_cachedHttpCredential = credential;
        }

        if (!credential.accessToken.empty() && !m_backgroundTasksRunning.load())
        {
            startBackgroundTasks();
            ELEGOO_LOG_INFO("Background tasks started after credential update");
            std::thread([this]()
                        {
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));

                    std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
                    m_agoraCredential = nullptr;
                    m_mqttCredential = nullptr;
                }
                {
                    std::lock_guard<std::mutex> lock(m_backgroundTasksMutex);
                    m_backgroundTasksWakeRequested.store(true);
                }
                m_backgroundTasksCv.notify_all(); })
                .detach();
        }
        return VoidResult::Success();
    }

    BizResult<HttpCredential> CloudService::getHttpCredential() const
    {
        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        if (!m_httpService)
        {
            return BizResult<HttpCredential>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
        }

        return BizResult<HttpCredential>::Ok(m_httpService->getCredential());
    }

    BizResult<HttpCredential> CloudService::refreshHttpCredential(const HttpCredential &credential)
    {
        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        if (!m_httpService)
        {
            return BizResult<HttpCredential>::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
        }

        // Check if the credential already exists in the history
        {
            std::shared_lock<std::shared_mutex> credentialsLock(m_credentialsMutex);
            auto it = std::find_if(m_credentialHistory.begin(), m_credentialHistory.end(),
                                   [&credential](const HttpCredential &storedCredential)
                                   {
                                       return storedCredential.userId == credential.userId &&
                                              storedCredential.accessToken == credential.accessToken &&
                                              storedCredential.refreshToken == credential.refreshToken;
                                   });

            if (it != m_credentialHistory.end())
            {
                return BizResult<HttpCredential>::Ok(m_cachedHttpCredential);
            }
        }

        auto result = m_httpService->refreshCredential(credential);
        if (result.isSuccess())
        {
            m_lastHttpErrorCode = ELINK_ERROR_CODE::SUCCESS;
            m_cachedHttpCredential = result.data.value();

            // Add the new credential to the history
            {
                std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
                m_credentialHistory.push_back(credential);
            }

            // Clear login from other device state to allow reconnection with refreshed credentials
            if (m_rtmService)
            {
                m_rtmService->clearLoginOtherDeviceState();
            }

            if (!credential.accessToken.empty() && !m_backgroundTasksRunning.load())
            {
                startBackgroundTasks();
                ELEGOO_LOG_INFO("Background tasks started after credential update");
                std::thread([this]()
                            {
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));

                        std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
                        m_agoraCredential = nullptr;
                        m_mqttCredential = nullptr;
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_backgroundTasksMutex);
                        m_backgroundTasksWakeRequested.store(true);
                    }
                    m_backgroundTasksCv.notify_all(); })
                    .detach();
            }
        }
        return result;
    }

    VoidResult CloudService::clearHttpCredential()
    {
        // Stop background tasks
        stopBackgroundTasks();
        VoidResult result = VoidResult::Success();
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

            if (m_httpService)
            {
                result = m_httpService->clearCredential();
            }

            // Disconnect other service connections
            if (m_mqttService)
            {
                m_mqttService->disconnect();
            }

            if (m_rtmService)
            {
                m_rtmService->disconnect();
            }
        }

        // Clean up credential cache
        {
            std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
            m_agoraCredential = nullptr;
            m_mqttCredential = nullptr;
        }

        setOnlineStatus(false);
        return result;
    }

    VoidResult CloudService::logout()
    {
        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        if (m_httpService)
        {
            return m_httpService->logout();
        }
        return VoidResult::Success();
    }

    GetRtcTokenResult CloudService::getRtcToken() const
    {
        std::shared_lock<std::shared_mutex> lock(m_credentialsMutex);

        GetRtcTokenResult result;
        if (m_agoraCredential)
        {
            result.data = RtcTokenData{
                m_agoraCredential->rtcUserId,
                m_agoraCredential->rtcToken,
                m_agoraCredential->rtcTokenExpireTime};
        }
        else
        {
            result.data = RtcTokenData{"", "", 0};
        }
        return result;
    }

    GetPrinterListResult CloudService::getPrinters()
    {
        GetPrinterListResult result;

        // First get the printer list from HTTP service, without holding lock
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            if (!m_httpService)
            {
                return GetPrinterListResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
            }
            result = m_httpService->getPrinters();
        }

        // If successful, update internal status
        if (result.isSuccess() && result.data.has_value())
        {
            // If the printer list has changed, update the printer information for each service
            bool printersChanged = false;

            {
                std::lock_guard<std::shared_mutex> printersLock(m_printersMutex);
                std::vector<PrinterInfo> oldPrinters;
                oldPrinters = std::move(m_printers);
                m_printers = result.data->printers;
                createMessageAdapters();
                if (oldPrinters.size() != m_printers.size())
                {
                    printersChanged = true;
                    ELEGOO_LOG_INFO("Printer list changed: old size = {}, new size = {}", oldPrinters.size(), m_printers.size());
                }
                else
                {
                    for (const auto &newPrinter : m_printers)
                    {
                        auto it = std::find_if(oldPrinters.begin(), oldPrinters.end(),
                                               [&newPrinter](const PrinterInfo &oldPrinter)
                                               { return oldPrinter.serialNumber == newPrinter.serialNumber; });
                        if (it == oldPrinters.end())
                        {
                            printersChanged = true;
                            break;
                        }
                    }
                }
            }
            if (printersChanged)
            {
                updateServicesWithAdapters();
            }
        }
        return result;
    }

    VoidResult CloudService::sendRtmMessage(const SendRtmMessageParams &params)
    {
        if (params.printerId.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        if (params.message.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Message cannot be empty");
        }

        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        if (!m_rtmService)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM service not initialized");
        }

        return m_rtmService->sendMessage(params);
    }

    void CloudService::startBackgroundTasks()
    {
        if (m_backgroundTasksRunning.load())
        {
            return; // Already running
        }

        m_backgroundTasksRunning.store(true);

        // Start connection monitoring task
        m_connectionMonitorThread = std::thread([this]()
                                                { connectionMonitorTask(); });

        ELEGOO_LOG_INFO("Background tasks started successfully");
    }

    void CloudService::stopBackgroundTasks()
    {
        if (!m_backgroundTasksRunning.load())
        {
            return; // Already stopped
        }

        m_backgroundTasksRunning.store(false);

        // Notify all waiting threads
        {
            std::lock_guard<std::mutex> lock(m_backgroundTasksMutex);
        }
        m_backgroundTasksCv.notify_all();

        if (m_connectionMonitorThread.joinable())
        {
            m_connectionMonitorThread.join();
        }

        ELEGOO_LOG_INFO("Background tasks stopped successfully");
    }

    void CloudService::connectionMonitorTask()
    {
        ELEGOO_LOG_INFO("Connection monitor task started");

        constexpr int PRINTER_STATUS_REFRESH_INTERVAL_COUNT = 3;  // Refresh printer status every 3 cycles (30 seconds)
        int printerStatusRefreshCounter = 0;

        while (m_backgroundTasksRunning.load())
        {
            // Wait for specified time or receive stop signal
            std::unique_lock<std::mutex> lock(m_backgroundTasksMutex);
            m_backgroundTasksCv.wait_for(lock,
                                         std::chrono::seconds(CONNECTION_MONITOR_INTERVAL_SECONDS),
                                         [this]()
                                         { return !m_backgroundTasksRunning.load() || m_backgroundTasksWakeRequested.load(); });
            // If explicitly awakened, clear wake flag
            if (m_backgroundTasksWakeRequested.load())
            {
                m_backgroundTasksWakeRequested.store(false);
            }
            try
            {
                refreshCredentials();
                retryConnections();
                
                // Poll printer status every 3 cycles (30 seconds)
                printerStatusRefreshCounter++;
                if (printerStatusRefreshCounter >= PRINTER_STATUS_REFRESH_INTERVAL_COUNT)
                {
                    printerStatusRefreshCounter = 0;
                    for (const auto &printerInfo : getCachedPrinters())
                    {
                        if (m_backgroundTasksRunning.load() == false)
                        {
                            break;
                        }
                        
                        // Skip status refresh if file is uploading for this printer
                        {
                            std::lock_guard<std::mutex> uploadLock(m_uploadingFilesMutex);
                            auto it = m_uploadingFiles.find(printerInfo.printerId);
                            if (it != m_uploadingFiles.end() && it->second)
                            {
                                continue; // Skip this printer
                            }
                        }
                        
                        PrinterStatusParams params;
                        params.printerId = printerInfo.printerId;
                        getPrinterStatusFromHttp(params);
                    }
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Error in connection monitor task: {}", e.what());
            }
        }

        ELEGOO_LOG_INFO("Connection monitor task ended");
    }

    void CloudService::refreshCredentials()
    {

        {
            std::lock_guard<std::mutex> lock(m_refreshCredentialsMutex);
            if (m_IsRefreshingCredentials)
            {
                return; // Already refreshing
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_refreshCredentialsMutex);
            m_IsRefreshingCredentials = true;
        }
        try
        {
            std::shared_lock<std::shared_mutex> servicesLock(m_servicesMutex);
            // Get Agora credentials
            if (m_httpService)
            {
                if (m_lastHttpErrorCode == ELINK_ERROR_CODE::SERVER_UNAUTHORIZED)
                {
                    ELEGOO_LOG_DEBUG("Previous HTTP error was unauthorized, skipping credential refresh.");
                    {
                        std::lock_guard<std::mutex> lock(m_refreshCredentialsMutex);
                        m_IsRefreshingCredentials = false;
                    }
                    return;
                }

                if (m_rtmService && m_rtmService->isConnected())
                {
                    // Already have credential, skip refresh
                }
                else
                {
                    if (!m_rtmService->isLoginOtherDevice())
                    {
                        auto agoraResult = m_httpService->getAgoraCredential();
                        if (agoraResult.isSuccess())
                        {
                            // Create new immutable credential pair
                            {
                                std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
                                m_agoraCredential = std::make_shared<const AgoraCredential>(agoraResult.value());
                            }
                            ELEGOO_LOG_INFO("Agora credential refreshed successfully");
                            EventCallback eventCallback = nullptr;
                            {
                                std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
                                eventCallback = m_eventCallback;
                            }

                            if (eventCallback)
                            {
                                BizEvent event;
                                event.method = MethodType::ON_RTC_TOKEN_CHANGED;
                                event.data = RtcTokenData{
                                    m_agoraCredential->rtcUserId,
                                    m_agoraCredential->rtcToken,
                                    m_agoraCredential->rtcTokenExpireTime};

                                eventCallback(event);
                            }
                        }
                        else if (agoraResult.code == ELINK_ERROR_CODE::SERVER_UNAUTHORIZED)
                        {
                            ELEGOO_LOG_WARN("HTTP credential token expired, user may need to re-login.");
                            m_lastHttpErrorCode = agoraResult.code;
                        }
                        else
                        {
                            ELEGOO_LOG_ERROR("Failed to refresh Agora credential: {}", agoraResult.message);
                        }
                    }
                }

                {
                    if (m_mqttService && m_mqttService->isConnected())
                    {
                    }
                    else
                    {
                        // Get MQTT credentials
                        auto mqttResult = m_httpService->getMqttCredential();
                        if (mqttResult.isSuccess())
                        {
                            // Create new immutable credential pair
                            {
                                std::lock_guard<std::shared_mutex> credentialsLock(m_credentialsMutex);
                                m_mqttCredential = std::make_shared<const MqttCredential>(mqttResult.value());
                            }
                            ELEGOO_LOG_INFO("MQTT credential refreshed successfully");
                        }
                        else if (mqttResult.code == ELINK_ERROR_CODE::SERVER_UNAUTHORIZED)
                        {
                            ELEGOO_LOG_WARN("HTTP credential token expired, user may need to re-login.");
                            m_lastHttpErrorCode = mqttResult.code;
                        }
                        else
                        {
                            ELEGOO_LOG_ERROR("Failed to refresh MQTT credential: {}", mqttResult.message);
                        }
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error refreshing credentials: {}", e.what());
        }
        {
            std::lock_guard<std::mutex> lock(m_refreshCredentialsMutex);
            m_IsRefreshingCredentials = false;
        }
    }

    void CloudService::retryConnections()
    {
        try
        {
            // Get current credential snapshot
            std::shared_ptr<const MqttCredential> mqttCred;
            std::shared_ptr<const AgoraCredential> agoraCred;

            {
                std::shared_lock<std::shared_mutex> credentialsLock(m_credentialsMutex);
                mqttCred = m_mqttCredential;
                agoraCred = m_agoraCredential;
            }

            {
                std::shared_lock<std::shared_mutex> servicesLock(m_servicesMutex);
                if (m_rtmService && m_rtmService->isLoginOtherDevice())
                {
                    ELEGOO_LOG_WARN("RTM logged in from another device, skipping reconnection attempts.");
                    setOnlineStatus(false);
                    return;
                }

                // Check RTM connection status
                if (m_rtmService && !m_rtmService->isConnected() && agoraCred && !agoraCred->userId.empty())
                {
                    ELEGOO_LOG_WARN("RTM connection lost, attempting to reconnect...");
                    auto rtmResult = m_rtmService->connect(*agoraCred);
                    if (!rtmResult.isSuccess())
                    {
                        ELEGOO_LOG_ERROR("RTM reconnection failed: {}", rtmResult.message);
                    }
                }

                // Check MQTT connection status
                if (m_mqttService && !m_mqttService->isConnected() && mqttCred && !mqttCred->host.empty())
                {
                    ELEGOO_LOG_WARN("MQTT connection lost, attempting to reconnect...");
                    auto mqttResult = m_mqttService->connect(*mqttCred);
                    if (!mqttResult.isSuccess())
                    {
                        ELEGOO_LOG_ERROR("MQTT reconnection failed: {}", mqttResult.message);
                    }
                }

                if ((m_rtmService && m_rtmService->isConnected()) ||
                    (m_mqttService && m_mqttService->isConnected()))
                {
                    setOnlineStatus(true);
                }
                else
                {
                    setOnlineStatus(false);
                }
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error retrying connections: {}", e.what());
        }
    }

    void CloudService::createMessageAdapters()
    {
        // Note: This method assumes the caller already holds a write lock on m_printersMutex

        // Create message adapter for each printer (if not already created)
        for (const auto &printer : m_printers)
        {
            if (m_messageAdapters.find(printer.printerId) == m_messageAdapters.end())
            {
                // Create corresponding adapter based on printer type
                switch (printer.printerType)
                {
                default:
                    // Default to using ElegooFdmCC2MessageAdapter
                    m_messageAdapters[printer.printerId] = std::make_shared<ElegooFdmCC2MessageAdapter>(printer);
                    std::string printerId = printer.printerId;

                    // When the adapted cached messages are non-continuous, a send callback will be triggered to refresh RTM messages, ensuring messages are up-to-date
                    m_messageAdapters[printer.printerId]->setMessageSendCallback([printerId, this](const PrinterBizRequest<std::string> &request)
                                                                                 { 
                                                                                        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
                                                                                       
                                                                                        if (m_rtmService) {
                                                                                            SendRtmMessageParams params;
                                                                                            params.printerId = printerId;
                                                                                            params.message = request.data;
                                                                                            m_rtmService->sendMessage(params);
                                                                                        } });
                    ELEGOO_LOG_INFO("Created default ElegooFdmCC2MessageAdapter for printer: {}", StringUtils::maskString(printer.printerId));
                    break;
                }
            }
        }

        // Remove adapters for printers that no longer exist
        auto it = m_messageAdapters.begin();
        while (it != m_messageAdapters.end())
        {
            bool printerExists = false;
            for (const auto &printer : m_printers)
            {
                if (printer.printerId == it->first)
                {
                    printerExists = true;
                    break;
                }
            }

            if (!printerExists)
            {
                ELEGOO_LOG_INFO("Removed message adapter for printer: {}", StringUtils::maskString(it->first));
                it = m_messageAdapters.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void CloudService::updateServicesWithAdapters()
    {
        // Get current status snapshot
        std::vector<PrinterInfo> printers;
        std::map<std::string, std::shared_ptr<IMessageAdapter>> adapters;

        {
            std::shared_lock<std::shared_mutex> printersLock(m_printersMutex);
            printers = m_printers;
            adapters = m_messageAdapters;
        }
        {
            std::shared_lock<std::shared_mutex> servicesLock(m_servicesMutex);

            if (m_httpService)
            {
                m_httpService->updatePrinters(printers);
            }

            if (m_mqttService)
            {
                m_mqttService->updatePrinters(printers);
                // Pass message adapters to MQTT service
                for (const auto &adapter : adapters)
                {
                    m_mqttService->setMessageAdapter(adapter.first, adapter.second);
                }
            }

            if (m_rtmService)
            {
                m_rtmService->updatePrinters(printers);
                // Pass message adapters to RTM service
                for (const auto &adapter : adapters)
                {
                    m_rtmService->setMessageAdapter(adapter.first, adapter.second);
                }
            }
        }
    }

    // Global coordinated remote service instance
    CloudService &getCloudService()
    {
        static CloudService instance;
        return instance;
    }

    BindPrinterResult CloudService::bindPrinter(const BindPrinterParams &params)
    {
        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
        if (!m_httpService)
        {
            return BindPrinterResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
        }

        if (params.pinCode.empty() || params.model.empty())
        {
            return BindPrinterResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Pin code and model cannot be empty");
        }
        std::string pinCode = params.pinCode;

        bool isManualBind = params.serialNumber.empty();
        int64_t timeoutSeconds = 20; // Default: 20 seconds for auto bind

        // First, get the serial number if it's empty
        std::string serialNumber = params.serialNumber;
        if (serialNumber.empty())
        {
            auto checkResult = m_httpService->checkPincode(params.model, params.pinCode);
            if (!checkResult.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to verify pincode: {}", checkResult.message);
                return BindPrinterResult::Error(checkResult.code, checkResult.message);
            }

            if (!checkResult.hasValue() || checkResult.value().serialNumber.empty())
            {
                ELEGOO_LOG_ERROR("Failed to get serial number from pincode");
                return BindPrinterResult::Error(ELINK_ERROR_CODE::INVALID_PIN_CODE, "Failed to get serial number");
            }

            serialNumber = checkResult.value().serialNumber;
            // int64_t expireTime = checkResult.value().expireTime;

            // For manual bind, calculate timeout based on expireTime
            // int64_t currentTime = TimeUtils::getCurrentTimestamp() / 1000;
            // timeoutSeconds = expireTime - currentTime;
            // if (timeoutSeconds <= 0)
            // {
            //     timeoutSeconds = 120; // Fallback to 120 seconds if expireTime is invalid
            // }
            // // It's possible that the user's computer time is inaccurate, resulting in an excessively long timeout
            // if (timeoutSeconds > 240)
            // {
            //     timeoutSeconds = 240; // Cap at 240 seconds
            // }

            timeoutSeconds = 240; // Set to 240 seconds for manual bind
            ELEGOO_LOG_INFO("Retrieved serial number from pincode: {}, timeout: {}s", StringUtils::maskString(serialNumber), timeoutSeconds);
        }

        // Check if the same SN device is already being bound
        {
            std::lock_guard<std::mutex> lock(m_bindStatesMutex);
            auto it = m_bindStates.find(serialNumber);
            if (it != m_bindStates.end() && it->second == BindState::Binding)
            {
                ELEGOO_LOG_ERROR("Bind printer already in progress for: {}", StringUtils::maskString(serialNumber));
                return BindPrinterResult::Error(
                    ELINK_ERROR_CODE::OPERATION_IN_PROGRESS,
                    "Bind operation already in progress for this printer");
            }
            // Mark as binding in progress
            m_bindStates[serialNumber] = BindState::Binding;
        }

        // Use RAII to ensure binding state is cleaned up in any case
        struct BindingStateGuard
        {
            std::map<std::string, BindState> &bindStates;
            std::mutex &bindStatesMutex;
            std::string serialNumber;
            bool released = false;

            BindingStateGuard(std::map<std::string, BindState> &states, std::mutex &mutex, const std::string &sn)
                : bindStates(states), bindStatesMutex(mutex), serialNumber(sn) {}

            ~BindingStateGuard()
            {
                if (!released)
                {
                    release();
                }
            }

            void release()
            {
                if (!released)
                {
                    std::lock_guard<std::mutex> lock(bindStatesMutex);
                    bindStates.erase(serialNumber);
                    released = true;
                    ELEGOO_LOG_DEBUG("Released binding state for printer: {}", StringUtils::maskString(serialNumber));
                }
            }
        };

        BindingStateGuard stateGuard(m_bindStates, m_bindStatesMutex, serialNumber);

        // First check service state and clear previous binding result
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            auto validationResult = validateRtmServiceState();
            if (!validationResult.isSuccess())
            {
                return validationResult;
            }

            if (!m_mqttService)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "MQTT service not initialized");
            }

            // Clear previous binding result
            m_mqttService->clearBindResult(serialNumber);
        }

        BizResult<std::string> result;

        // Create a new params with the obtained serialNumber
        BindPrinterParams bindParams = params;
        bindParams.serialNumber = serialNumber;

        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            if (!m_httpService)
            {
                return BindPrinterResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
            }
            result = m_httpService->bindPrinter(bindParams, isManualBind);
        }

        if (!result.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to send bind printer request: {}", result.message);
            return BindPrinterResult::Error(result.code, result.message);
        }

        // After pre-binding succeeds, need to listen for RTM message to confirm binding result
        ELEGOO_LOG_INFO("Bind printer request sent successfully for printer: {}", StringUtils::maskString(serialNumber));

        // Optimize waiting logic to avoid deadlock
        const auto startTime = std::chrono::steady_clock::now();
        // Determine timeout duration based on binding method
        // Auto bind: 20 seconds (default), Manual bind: calculated from expireTime
        const auto timeoutDuration = std::chrono::seconds(timeoutSeconds);
        const auto checkInterval = std::chrono::milliseconds(100);
        const auto periodicQueryInterval = std::chrono::seconds(10);
        auto lastQueryTime = startTime;

        while (true)
        {
            // Check for cancellation request
            {
                std::lock_guard<std::mutex> lock(m_bindStatesMutex);
                auto it = m_bindStates.find(serialNumber);
                if (it != m_bindStates.end() && it->second == BindState::Cancelled)
                {
                    ELEGOO_LOG_WARN("Bind printer operation was cancelled for: {}", StringUtils::maskString(serialNumber));
                    return BindPrinterResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Bind operation was cancelled by user");
                }
            }

            // Check if service is still valid (without holding other locks)
            if (!m_initialized.load())
            {
                ELEGOO_LOG_WARN("Network service was cleaned up during bind printer operation for: {}", StringUtils::maskString(serialNumber));
                return BindPrinterResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Network service was cleaned up");
            }

            // Check binding result
            int bindResult = 0;
            {
                std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
                if (!m_mqttService)
                {
                    return BindPrinterResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "MQTT service not initialized");
                }

                bindResult = m_mqttService->getBindResult(serialNumber);
                if (bindResult == 1)
                {
                    m_mqttService->clearBindResult(serialNumber);
                    ELEGOO_LOG_INFO("Printer bound successfully: {}", StringUtils::maskString(serialNumber));
                }
                else if (bindResult == 2)
                {
                    ELEGOO_LOG_DEBUG("Current bind result for printer {}: {}", StringUtils::maskString(serialNumber), bindResult);
                    m_mqttService->clearBindResult(serialNumber);
                    return BindPrinterResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "User rejected the bind operation");
                }
            }
            bool isTimeout = false;
            // Check if timeout
            auto currentTime = std::chrono::steady_clock::now();
            if ((currentTime - startTime >= timeoutDuration))
            {
                ELEGOO_LOG_ERROR("Bind printer result timeout for printer: {}", StringUtils::maskString(serialNumber));
                isTimeout = true;
            }

            // Check if periodic query is needed (every 5 seconds)
            bool shouldPeriodicQuery = (currentTime - lastQueryTime >= periodicQueryInterval);

            if (isTimeout || bindResult == 1 || shouldPeriodicQuery)
            {
                // Verify that the device exists in getPrinters result before returning success
                GetPrinterListResult printersResult = getPrinters();

                // Update last query time after performing query
                lastQueryTime = currentTime;

                if (printersResult.isSuccess() && printersResult.data.has_value())
                {
                    // Search for the printer with the specified serial number
                    bool printerFound = false;
                    PrinterInfo foundPrinter;

                    for (const auto &printer : printersResult.data->printers)
                    {
                        if (printer.serialNumber == serialNumber)
                        {
                            printerFound = true;
                            foundPrinter = printer;
                            break;
                        }
                    }

                    if (printerFound)
                    {
                        ELEGOO_LOG_INFO("Printer with SN {} found in getPrinters result", StringUtils::maskString(serialNumber));
                        BindPrinterResult bindResult;
                        bindResult.data = BindPrinterData{true, foundPrinter};
                        return bindResult;
                    }
                    else
                    {
                        // For periodic queries, if printer not found yet, continue waiting
                        if (isTimeout)
                        {
                            ELEGOO_LOG_ERROR("Printer with SN {} not found after bind operation", StringUtils::maskString(serialNumber));
                            return BindPrinterResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "Printer not found after bind operation");
                        }
                    }
                }
                else
                {
                    // For periodic queries, if query fails, continue waiting (unless timeout)
                    ELEGOO_LOG_ERROR("Failed to get printer list to verify binding: {}", printersResult.message);
                    return BindPrinterResult::Error(printersResult.code, "Failed to verify binding: " + printersResult.message);
                }
            }

            // Use condition variable to wait, can be interrupted by cleanup()
            std::unique_lock<std::mutex> waitLock(m_backgroundTasksMutex);
            m_backgroundTasksCv.wait_for(waitLock, checkInterval, [this]()
                                         { return !m_backgroundTasksRunning.load() || m_backgroundTasksWakeRequested.load(); });

            // If explicitly awakened (e.g., credential refresh), clear flag and continue checking binding result
            if (m_backgroundTasksWakeRequested.load())
            {
                m_backgroundTasksWakeRequested.store(false);
            }

            // If background task has stopped (usually means service is shutting down), then exit`
            if (!m_backgroundTasksRunning.load())
            {
                ELEGOO_LOG_WARN("Background tasks stopped during bind printer operation for: {}", StringUtils::maskString(serialNumber));
                return BindPrinterResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Service is shutting down");
            }
        }
    }

    VoidResult CloudService::cancelBindPrinter(const CancelBindPrinterParams &params)
    {
        if (params.serialNumber.empty())
        {
            // cancel all bindings
            std::lock_guard<std::mutex> lock(m_bindStatesMutex);
            for (auto &pair : m_bindStates)
            {
                if (pair.second == BindState::Binding)
                {
                    pair.second = BindState::Cancelled;
                    ELEGOO_LOG_INFO("Bind printer operation cancelled for: {}", StringUtils::maskString(pair.first));
                }
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(m_bindStatesMutex);
            auto it = m_bindStates.find(params.serialNumber);
            if (it != m_bindStates.end() && it->second == BindState::Binding)
            {
                it->second = BindState::Cancelled;
                ELEGOO_LOG_INFO("Bind printer operation cancelled for: {}", StringUtils::maskString(params.serialNumber));
            }
        }

        // Wake up the waiting thread
        {
            std::lock_guard<std::mutex> lock(m_backgroundTasksMutex);
            m_backgroundTasksWakeRequested.store(true);
        }
        m_backgroundTasksCv.notify_all();

        return VoidResult::Success();
    }

    VoidResult CloudService::unbindPrinter(const UnbindPrinterParams &params)
    {
        if (params.serialNumber.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Serial number cannot be empty");
        }

        VoidResult ret;
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

            auto validationResult = validateHttpServiceState();
            if (!validationResult.isSuccess())
            {
                return validationResult;
            }

            ret = m_httpService->unbindPrinter(params);
        }
        getPrinters(); // Refresh printer list after unbinding
        return ret;
    }

    ConnectPrinterResult CloudService::connectPrinter(const ConnectPrinterParams &params)
    {
        if (params.serialNumber.empty())
        {
            return ConnectPrinterResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Serial number cannot be empty");
        }

        if (params.printerId.empty())
        {
            return ConnectPrinterResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        std::string serialNumber = params.serialNumber;
        PrinterInfo printer;
        {
            std::shared_lock<std::shared_mutex> lock(m_printersMutex);
            auto it = std::find_if(m_printers.begin(), m_printers.end(),
                                   [&serialNumber](const PrinterInfo &p)
                                   { return p.serialNumber == serialNumber; });
            if (it == m_printers.end())
            {
                return ConnectPrinterResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found: " + serialNumber);
            }
            printer = *it;
            printer.printerId = params.printerId;
        }
        ELEGOO_LOG_INFO("Printer connected successfully: {}", StringUtils::maskString(serialNumber));

        ConnectPrinterResult result;
        result.data = ConnectPrinterData{true, printer};
        return result;
    }

    DisconnectPrinterResult CloudService::disconnectPrinter(const DisconnectPrinterParams &params)
    {
        return DisconnectPrinterResult::Success();
    }

    GetFileListResult CloudService::getFileList(const GetFileListParams &params)
    {
        if (params.printerId.empty())
        {
            return GetFileListResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        auto validationResult = validateHttpServiceState();
        if (!validationResult.isSuccess())
        {
            return validationResult;
        }

        return m_httpService->getFileList(params);
    }

    GetFileDetailResult CloudService::getFileDetail(const GetFileDetailParams &params)
    {
        if (params.printerId.empty())
        {
            return GetFileDetailResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        if (params.fileName.empty())
        {
            return GetFileDetailResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File name cannot be empty");
        }

        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        auto validationResult = validateHttpServiceState();
        if (!validationResult.isSuccess())
        {
            return validationResult;
        }

        return m_httpService->getFileDetail(params);
    }

    PrintTaskListResult CloudService::getPrintTaskList(const PrintTaskListParams &params)
    {
        if (params.printerId.empty())
        {
            return PrintTaskListResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        auto validationResult = validateHttpServiceState();
        if (!validationResult.isSuccess())
        {
            return validationResult;
        }

        return m_httpService->getPrintTaskList(params);
    }

    DeletePrintTasksResult CloudService::deletePrintTasks(const DeletePrintTasksParams &params)
    {
        if (params.printerId.empty())
        {
            return DeletePrintTasksResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        if (params.taskIds.empty())
        {
            return DeletePrintTasksResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Task IDs cannot be empty");
        }

        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);

        auto validationResult = validateHttpServiceState();
        if (!validationResult.isSuccess())
        {
            return validationResult;
        }

        return m_httpService->deletePrintTasks(params);
    }

    // Service state check helper functions
    VoidResult CloudService::validateHttpServiceState() const
    {
        if (!m_initialized.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "Network service not initialized");
        }

        // Note: Here it is assumed that the caller already holds m_servicesMutex
        if (!m_httpService)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
        }

        return VoidResult::Success();
    }

    VoidResult CloudService::validateRtmServiceState() const
    {
        // Note: Here it is assumed that the caller already holds m_servicesMutex
        if (!m_rtmService)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM service not initialized");
        }

        return VoidResult::Success();
    }

    void CloudService::setOnlineStatus(bool isOnline)
    {
        if (m_isOnline != isOnline)
        {
            m_isOnline = isOnline;
            EventCallback eventCallback = nullptr;
            {
                std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
                eventCallback = m_eventCallback;
            }
            if (eventCallback)
            {
                BizEvent event;
                event.method = MethodType::ON_ONLINE_STATUS_CHANGED;
                event.data = OnlineStatusData{isOnline};

                eventCallback(event);
            }
            if (!isOnline)
            {
                std::lock_guard<std::mutex> lock(m_bindStatesMutex);
                for (auto &pair : m_bindStates)
                {
                    if (pair.second == BindState::Binding)
                    {
                        pair.second = BindState::Cancelled;
                        ELEGOO_LOG_TRACE("Bind printer operation cancelled due to offline status for: {}", StringUtils::maskString(pair.first));
                    }
                }
            }
        }
    }
    StartPrintResult CloudService::startPrint(const StartPrintParams &params)
    {
        if (params.fileName.empty())
        {
            return StartPrintResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File name cannot be empty");
        }
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)

        BizRequest request;
        request.method = MethodType::START_PRINT;
        request.params = params;
        return m_rtmService->executeRequest<std::monostate>(request, "StartPrint", std::chrono::milliseconds(5000));
    }

    VoidResult CloudService::stopPrint(const StopPrintParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::STOP_PRINT;
        request.params = params;
        return m_rtmService->executeRequest<std::monostate>(request, "StopPrint", std::chrono::milliseconds(3000));
    }

    VoidResult CloudService::pausePrint(const PausePrintParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::PAUSE_PRINT;
        request.params = params;
        return m_rtmService->executeRequest<std::monostate>(request, "PausePrint", std::chrono::milliseconds(3000));
    }

    VoidResult CloudService::resumePrint(const ResumePrintParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::RESUME_PRINT;
        request.params = params;
        return m_rtmService->executeRequest<std::monostate>(request, "ResumePrint", std::chrono::milliseconds(3000));
    }

    GetCanvasStatusResult CloudService::getCanvasStatus(const GetCanvasStatusParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::GET_CANVAS_STATUS;
        request.params = params;
        return m_rtmService->executeRequest<CanvasStatus>(request, "GetCanvasStatus", std::chrono::milliseconds(3000));
    }

    SetAutoRefillResult CloudService::setAutoRefill(const SetAutoRefillParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::SET_AUTO_REFILL;
        request.params = params;
        return m_rtmService->executeRequest<std::monostate>(request, "SetAutoRefill", std::chrono::milliseconds(3000));
    }

    PrinterStatusResult CloudService::getPrinterStatus(const PrinterStatusParams &params)
    {
        // BizRequest request;
        // request.method = MethodType::GET_PRINTER_STATUS;
        // request.params = params;

        return getPrinterStatusFromHttp(params);
        // return executeRequestSync<PrinterStatusData>(params.printerId, request, "GetPrinterStatus", true, std::chrono::milliseconds(3000));
    }

    PrinterAttributesResult CloudService::getPrinterAttributes(const PrinterAttributesParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::GET_PRINTER_ATTRIBUTES;
        request.params = params;
        return m_rtmService->executeRequest<PrinterAttributesData>(request, "GetPrinterAttributes", std::chrono::milliseconds(3000));
    }

    VoidResult CloudService::refreshPrinterAttributes(const PrinterAttributesParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::GET_PRINTER_ATTRIBUTES;
        request.params = params;
        m_rtmService->executeRequest<PrinterAttributesData>(request, "GetPrinterAttributes", std::chrono::milliseconds(1));
        return VoidResult::Success();
    }

    /**
     * Refresh printer status, The result will be notified through events
     * @param params Printer status parameters
     * @return Operation result
     */
    VoidResult CloudService::refreshPrinterStatus(const PrinterStatusParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::GET_PRINTER_STATUS;
        request.params = params;
        m_rtmService->executeRequest<PrinterStatusData>(request, "GetPrinterStatus", std::chrono::milliseconds(1));
        return VoidResult::Success();
    }

    BizResult<std::string> CloudService::getPrinterStatusRaw(const PrinterStatusParams &params)
    {
        if (params.printerId.empty())
        {
            return BizResult<std::string>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        BizResult<nlohmann::json> result;

        // Get printer status
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            auto validationResult = validateHttpServiceState();
            if (!validationResult.isSuccess())
            {
                return BizResult<std::string>::Error(validationResult.code, validationResult.message);
            }

            result = m_httpService->getPrinterStatus(params.printerId);
        }

        // Update message adapter cache
        if (result.isSuccess() && result.data.has_value())
        {
            std::shared_lock<std::shared_mutex> printersLock(m_printersMutex);
            if (m_messageAdapters.find(params.printerId) != m_messageAdapters.end())
            {
                nlohmann::json statusJson = nlohmann::json::object();
                statusJson["method"] = 1002;
                statusJson["id"] = 0;
                statusJson["result"] = result.data.value();
                // Used to refresh the status cache of the message adapter
                m_messageAdapters[params.printerId]->convertToEvent(statusJson.dump());
            }
        }

        if (!result.isSuccess() || !result.data.has_value())
        {
            return BizResult<std::string>::Error(result.code, result.message);
        }
        return BizResult<std::string>::Ok(result.data.value().dump());
    }

    PrinterStatusResult CloudService::getPrinterStatusFromHttp(const PrinterStatusParams &params)
    {
        if (params.printerId.empty())
        {
            return PrinterStatusResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        BizResult<nlohmann::json> result;
        // Get printer status
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            auto validationResult = validateHttpServiceState();
            if (!validationResult.isSuccess())
            {
                return PrinterStatusResult::Error(validationResult.code, validationResult.message);
            }

            result = m_httpService->getPrinterStatus(params.printerId);
        }

        // Handle status conversion
        if (result.isSuccess() && result.data.has_value())
        {
            std::shared_lock<std::shared_mutex> printersLock(m_printersMutex);
            if (m_messageAdapters.find(params.printerId) != m_messageAdapters.end())
            {
                nlohmann::json statusJson = nlohmann::json::object();
                statusJson["method"] = 1002;
                statusJson["id"] = 0;
                statusJson["result"] = result.data.value();
                auto response = m_messageAdapters[params.printerId]->convertToEvent(statusJson.dump());
                if (response.isValid())
                {
                    PrinterStatusResult statusResult;
                    statusResult.data = response.data.value();
                    return statusResult;
                }
                else
                {
                    return PrinterStatusResult::Error(ELINK_ERROR_CODE::SERVER_INVALID_RESPONSE, "Failed to parse printer status data");
                }
            }
            else
            {
                return PrinterStatusResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Message adapter not found for printer: " + params.printerId);
            }
        }
        else
        {
            return PrinterStatusResult::Error(result.code, result.message);
        }
    }

    FileUploadResult CloudService::uploadFile(
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback)
    {
        ELEGOO_LOG_INFO("Starting file upload to printer: {}", params.fileName);
        // Validate parameters first
        if (params.printerId.empty())
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }

        if (params.localFilePath.empty())
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Local file path cannot be empty");
        }
        // Clear any previous cancellation flag for this printer
        {
            std::lock_guard<std::mutex> lock(m_uploadCancellationsMutex);
            m_uploadCancellations[params.printerId] = false;
        }

        // Check if a file is already being uploaded
        {
            std::lock_guard<std::mutex> lock(m_uploadingFilesMutex);
            auto it = m_uploadingFiles.find(params.printerId);
            if (it != m_uploadingFiles.end() && it->second)
            {
                ELEGOO_LOG_ERROR("File upload already in progress for printer: {}", StringUtils::maskString(params.printerId));
                return FileUploadResult::Error(
                    ELINK_ERROR_CODE::OPERATION_IN_PROGRESS,
                    "File upload already in progress for this printer");
            }
            // Mark as uploading
            m_uploadingFiles[params.printerId] = true;
        }

        // Use RAII to ensure upload state is cleaned up in any case
        struct UploadGuard
        {
            CloudService *self;
            std::string printerId;
            bool released = false;

            UploadGuard(CloudService *s, const std::string &id) : self(s), printerId(id) {}

            ~UploadGuard()
            {
                if (!released)
                {
                    release();
                }
            }

            void release()
            {
                if (released)
                    return;
                released = true;

                // 1. Try to refresh printer status and clear MQTT state
                try
                {
                    // Try to refresh full printer status (ignore any errors)
                    if (self->m_initialized.load())
                    {
                        PrinterStatusParams statusParams;
                        statusParams.printerId = printerId;
                        try
                        {
                            self->getPrinterStatusRaw(statusParams);
                        }
                        catch (...)
                        {
                        }
                    }

                    // Clear MQTT upload state (if exists)
                    {
                        std::shared_lock<std::shared_mutex> lock(self->m_servicesMutex);
                        if (self->m_mqttService)
                        {
                            self->m_mqttService->setFileUploading(printerId, false, 0);
                        }
                    }
                }
                catch (...)
                {
                }

                // 2. Clear uploadingFiles state
                {
                    std::lock_guard<std::mutex> lock(self->m_uploadingFilesMutex);
                    self->m_uploadingFiles[printerId] = false;
                }
                ELEGOO_LOG_INFO("Released upload state for printer: {}", StringUtils::maskString(printerId));
            }
        };

        UploadGuard uploadGuard(this, params.printerId);
        std::string serialNumber = getSerialNumberByPrinterId(params.printerId);
        if (serialNumber.empty())
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found: " + params.printerId);
        }

        if (!FileUtils::fileExists(params.localFilePath))
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "Local file not found: " + params.localFilePath);
        }
        std::string fileName = params.fileName;
        if (fileName.empty())
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File name cannot be empty");
        }

        // Get file name and extension
        std::string extension = FileUtils::getFileExtension(fileName);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (extension != "gcode" && extension != "3mf")
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Unsupported file extension: " + extension);
        }

        std::string md5 = FileUtils::calculateMD5(params.localFilePath);
        if (md5.empty())
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to calculate MD5 for file: " + params.localFilePath);
        }

        // Get userId from HTTP credential
        std::string userId;
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            if (!m_httpService)
            {
                return FileUploadResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "HTTP service not initialized");
            }
            userId = m_httpService->getCredential().userId;
        }

        if (userId.empty())
        {
            return FileUploadResult::Error(ELINK_ERROR_CODE::SERVER_UNAUTHORIZED, "User not logged in");
        }

        // Generate unique server file name using userId + MD5 to prevent conflicts when multiple users upload files with the same name
        std::string originalFileName = fileName; // Keep original file name for display
        if (userId.length() > 6)
        {
            userId = userId.substr(userId.length() - 6);
        }

        if (params.printerId.length() > 6)
        {
            userId += "_" + params.printerId.substr(params.printerId.length() - 6);
        }

        std::string serverFileName = userId + "_" + md5 + "." + extension;

        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            auto validationResult = validateRtmServiceState();
            if (!validationResult.isSuccess())
            {
                return FileUploadResult::Error(validationResult.code, validationResult.message);
            }
        }

        // Determine if the file already exists, if creation time exists, the file is already present; this time is used to determine upload success later
        int64_t fileCreateTime = 0;
        {
            GetFileDetailParams detailParams;
            detailParams.fileName = originalFileName;
            detailParams.printerId = params.printerId;
            auto r = m_httpService->getFileDetail(detailParams, false);
            if (r.code == ELINK_ERROR_CODE::SUCCESS)
            {
                if (r.hasValue())
                {
                    fileCreateTime = r.value().createTime;
                }
                ELEGOO_LOG_INFO("File already exists, creation time: {}", fileCreateTime);
            }
        }

        // Set MQTT service upload state to uploading before starting HTTP upload
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            if (m_mqttService)
            {
                m_mqttService->setFileUploading(params.printerId, true, 0);
                ELEGOO_LOG_INFO("Set uploading state before HTTP upload for printer: {}", StringUtils::maskString(params.printerId));
            }
        }

        int64_t fileSize = 0;
        int oldPercentage = 0;
        // Upload using serverFileName to prevent conflicts, but the actual file name on the device will remain as originalFileName
        auto result = m_httpService->uploadFile(serverFileName, params.localFilePath, [&](uint64_t current, uint64_t total)
                                                {
                                                   // Check for cancellation
                                                   {
                                                       std::lock_guard<std::mutex> lock(m_uploadCancellationsMutex);
                                                       auto it = m_uploadCancellations.find(params.printerId);
                                                       if (it != m_uploadCancellations.end() && it->second)
                                                       {
                                                           ELEGOO_LOG_INFO("File upload cancelled during HTTP upload for printer: {}", StringUtils::maskString(params.printerId));
                                                           return false; // Cancel upload
                                                       }
                                                   }

                                                   if (progressCallback)
                                                   {
                                                        fileSize = total;
                                                        // Because file upload is segmented: first upload, then device downloads, so this progress is half of the total
                                                        FileUploadProgressData progress;
                                                        int percentage = total > 0 ? static_cast<int>((current * 100.0) / total) : 0;
                                                        if (percentage != oldPercentage)
                                                        {
                                                            oldPercentage = percentage;
                                                            progress.percentage = percentage / 2;
                                                            progress.totalBytes = total;
                                                            progress.uploadedBytes = current / 2;
                                                            progress.printerId = params.printerId;
                                                            
                                                            // Update MQTT service HTTP upload progress
                                                            {
                                                                std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
                                                                if (m_mqttService)
                                                                {
                                                                    m_mqttService->setFileUploading(params.printerId, true, percentage / 2);
                                                                }
                                                            }
                                                            
                                                            return progressCallback(progress);
                                                        }
                                                   }
                                                   return true; });
        if (!result.isSuccess())
        {
            return FileUploadResult{result.code, result.message};
        }

        ELEGOO_LOG_INFO("File uploaded to server successfully: {} (server name: {}), MD5: {}", originalFileName, serverFileName, md5);

        // Cancel historical upload tasks, if any
        std::string taskId = serialNumber;
        {
            CancelPrinterDownloadFileParams cancelParams;
            cancelParams.printerId = params.printerId;
            cancelParams.taskId = taskId;

            BizRequest request;
            request.method = MethodType::CANCEL_PRINTER_DOWNLOAD_FILE;
            request.params = cancelParams;
            auto ret = m_rtmService->executeRequest<std::monostate>(request, "CancelPrinterDownloadFile", std::chrono::milliseconds(5000));
            if (!ret.isSuccess())
            {
                ELEGOO_LOG_INFO("No existing download task to cancel for printer: {}", StringUtils::maskString(params.printerId));
            }
        }

        // Sleep 1 second to ensure cancel command is processed
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Notify printer to download file, use originalFileName to keep the original name on the device
        {
            ELEGOO_LOG_INFO("Starting printer download for file: {} to printer: {}", originalFileName, StringUtils::maskString(params.printerId));
            m_rtmService->resetDownloadFileStatus(params.printerId);

            SetPrinterDownloadFileParams downloadParams;
            downloadParams.fileName = originalFileName; // Use original file name on the device
            downloadParams.fileUrl = result.data.value_or("");
            downloadParams.printerId = params.printerId;
            downloadParams.taskId = taskId;
            downloadParams.md5 = md5;

            BizRequest request;
            request.method = MethodType::SET_PRINTER_DOWNLOAD_FILE;
            request.params = downloadParams;
            auto ret = m_rtmService->executeRequest<std::monostate>(request, "SetPrinterDownloadFile", std::chrono::milliseconds(15000));
            if (!ret.isSuccess())
            {
                ELEGOO_LOG_ERROR("Failed to set printer download file: {}", ret.message);
                return FileUploadResult{ret.code, ret.message};
            }
        }

        // Periodically check file upload progress, if no update for over 30 seconds, consider upload timeout/failure
        int lastProgress = -1;

        const auto timeoutDuration = std::chrono::seconds(30);
        const auto checkInterval = std::chrono::milliseconds(500);
        bool isTimeout = false;
        bool isComplete = false; // Set to true to enable timeout check
        while (true)
        {
            bool isCancelled = false;
            // Check for cancellation
            {
                std::lock_guard<std::mutex> lock(m_uploadCancellationsMutex);
                auto it = m_uploadCancellations.find(params.printerId);
                if (it != m_uploadCancellations.end() && it->second)
                {
                    ELEGOO_LOG_INFO("File upload cancelled during printer download phase for printer: {}", StringUtils::maskString(params.printerId));
                    isCancelled = true;
                    break;
                }
            }

            // Check if service is still valid (without holding other locks)
            if (!m_initialized.load())
            {
                ELEGOO_LOG_WARN("Network service was cleaned up during file upload operation for: {}", StringUtils::maskString(params.printerId));
                return FileUploadResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Network service was cleaned up");
            }

            std::this_thread::sleep_for(checkInterval);

            auto currentTime = std::chrono::steady_clock::now();
            auto status = m_rtmService->getDownloadFileStatus(params.printerId);

            // Check if upload is complete
            if (status.status == 1) // 1-Completed
            {
                ELEGOO_LOG_INFO("File upload completed, file name: {} to printer: {}", originalFileName, StringUtils::maskString(params.printerId));
                isComplete = true;
                break;
                // return FileUploadResult::Success();
            }
            else if (status.status == 2) // 2-Cancelled
            {
                ELEGOO_LOG_WARN("File upload cancelled, file name: {} to printer: {}", originalFileName, StringUtils::maskString(params.printerId));
                return FileUploadResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "File upload was cancelled");
            }
            else if (status.status == 3) // 3-Failed
            {
                ELEGOO_LOG_ERROR("File upload failed, file name: {} to printer: {}", originalFileName, StringUtils::maskString(params.printerId));
                return FileUploadResult::Error(ELINK_ERROR_CODE::FILE_TRANSFER_FAILED, "File upload failed with exception");
            }

            // Call progress callback
            if (progressCallback)
            {
                FileUploadProgressData progress;
                progress.percentage = 50 + (status.progress / 2); // 50%~100% for printer download phase
                progress.totalBytes = fileSize;
                progress.uploadedBytes = fileSize / 2 + (fileSize * status.progress / 200.0);
                progress.printerId = params.printerId;

                // Only call callback when progress changes
                if (lastProgress != status.progress)
                {
                    lastProgress = status.progress;
                    // Update MQTT service upload progress
                    {
                        std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
                        if (m_mqttService)
                        {
                            m_mqttService->setFileUploading(params.printerId, true, progress.percentage);
                        }
                    }
                }

                if (!progressCallback(progress))
                {
                    ELEGOO_LOG_INFO("File upload cancelled by user callback for printer: {}", StringUtils::maskString(params.printerId));
                    isCancelled = true;
                    break;
                }
            }
            if (isCancelled)
            {
                // User cancelled upload
                CancelPrinterDownloadFileParams cancelParams;
                cancelParams.printerId = params.printerId;
                cancelParams.taskId = taskId;

                BizRequest request;
                request.method = MethodType::CANCEL_PRINTER_DOWNLOAD_FILE;
                request.params = cancelParams;
                m_rtmService->executeRequest<std::monostate>(request, "CancelPrinterDownloadFile", std::chrono::milliseconds(5000));

                return FileUploadResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "File upload cancelled by user");
            }
            // Use status.lastUpdatedTime to check for timeout
            if (currentTime - status.lastUpdatedTime >= timeoutDuration)
            {
                isTimeout = true;
                ELEGOO_LOG_WARN("File upload timeout: no progress update for more than 30 seconds");
                break;
            }
        }
        int retryCount = 0;
        do
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            ELEGOO_LOG_INFO("Verifying uploaded file detail, attempt {} for printer: {}", retryCount + 1, StringUtils::maskString(params.printerId));
            // Check if service is still valid (without holding other locks)
            if (!m_initialized.load())
            {
                ELEGOO_LOG_WARN("Network service was cleaned up during file upload operation for: {}", StringUtils::maskString(params.printerId));
                return FileUploadResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Network service was cleaned up");
            }
            GetFileDetailParams detailParams;
            detailParams.fileName = originalFileName;
            detailParams.printerId = params.printerId;
            auto r = m_httpService->getFileDetail(detailParams, false);
            if (r.code == ELINK_ERROR_CODE::SUCCESS)
            {
                if (r.hasValue())
                {
                    // Verify if file was updated by comparing creation time
                    if (r.value().createTime != fileCreateTime)
                    {
                        ELEGOO_LOG_INFO("File uploaded to printer successfully: {}", StringUtils::maskString(params.printerId));
                        ELEGOO_LOG_INFO("Uploaded file detail - name: {}, size: {}, creation time: {}",
                                        r.data->fileName, r.data->size, r.data->createTime);

                        return FileUploadResult::Success();
                    }
                }
            }
        } while (retryCount++ < 5);

        if (isComplete)
        {
            return FileUploadResult::Success();
        }
        else
        {
            if (isTimeout)
            {
                ELEGOO_LOG_WARN("File upload timeout, file name: {} to printer: {}", originalFileName, StringUtils::maskString(params.printerId));
                return FileUploadResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "File upload timeout: no progress update for more than 30 seconds");
            }
            else
            {
                ELEGOO_LOG_ERROR("File upload failed, file name: {} to printer: {}", originalFileName, StringUtils::maskString(params.printerId));
                return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "File upload failed");
            }
        }
    }

    VoidResult CloudService::cancelFileUpload(const CancelFileUploadParams &params)
    {
        ELEGOO_LOG_INFO("Cancelling file upload for printer: {}", StringUtils::maskString(params.printerId));
        if (params.printerId.empty())
        {
            ELEGOO_LOG_ERROR("Printer ID cannot be empty for cancelFileUpload");
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Printer ID cannot be empty");
        }
        // Set cancellation flag
        {
            std::lock_guard<std::mutex> lock(m_uploadCancellationsMutex);
            m_uploadCancellations[params.printerId] = true;
        }

        // Check if there's an active upload
        {
            std::lock_guard<std::mutex> lock(m_uploadingFilesMutex);
            auto it = m_uploadingFiles.find(params.printerId);
            if (it == m_uploadingFiles.end() || !it->second)
            {
                ELEGOO_LOG_WARN("No active file upload found for printer: {}", StringUtils::maskString(params.printerId));
                return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "No active file upload found for this printer");
            }
        }

        ELEGOO_LOG_INFO("File upload cancellation flag set for printer: {}", StringUtils::maskString(params.printerId));
        return VoidResult::Success();
    }

    VoidResult CloudService::updatePrinterName(const UpdatePrinterNameParams &params)
    {
        VALIDATE_PRINTER_AND_RTM_SERVICE(VoidResult)
        BizRequest request;
        request.method = MethodType::UPDATE_PRINTER_NAME;
        request.params = params;
        auto ret = m_rtmService->executeRequest<std::monostate>(request, "UpdatePrinterName", std::chrono::milliseconds(3000));
        if (ret.isError())
        {
            return ret;
        }
        {
            std::shared_lock<std::shared_mutex> lock(m_servicesMutex);
            auto validationResult = validateHttpServiceState();
            if (validationResult.isSuccess())
            {
                m_httpService->updatePrinterName(params); // Update cloud cache
            }
        }
        return ret;
    }

}
