#include "services/mqtt_service.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include <nlohmann/json.hpp>
#include "types/internal/internal.h"
#include "types/internal/json_serializer.h"
#include "utils/json_utils.h"

namespace
{
    void emitConnectionStatusEvent(const elink::EventCallback &eventCallback,
                                   const std::string &printerId,
                                   elink::ConnectionStatus status)
    {
        if (!eventCallback)
        {
            return;
        }

        elink::BizEvent event;
        elink::ConnectionStatusData eventData;
        eventData.printerId = printerId;
        eventData.status = status;
        event.method = elink::MethodType::ON_CONNECTION_STATUS;
        event.data = eventData;
        eventCallback(event);
    }

    void emitOfflinePrinterStatusEvent(const elink::EventCallback &eventCallback,
                                       const std::string &printerId)
    {
        if (!eventCallback)
        {
            return;
        }

        elink::BizEvent statusEvent;
        statusEvent.method = elink::MethodType::ON_PRINTER_STATUS;
        elink::PrinterStatusData printerStatusEvent;
        printerStatusEvent.printerId = printerId;
        printerStatusEvent.printerStatus.state = elink::PrinterState::OFFLINE;
        statusEvent.data = printerStatusEvent;
        eventCallback(statusEvent);
    }

    void emitRawPrinterEvent(const elink::EventCallback &eventCallback,
                             const std::string &printerId,
                             const std::shared_ptr<elink::IMessageAdapter> &adapter)
    {
        if (!eventCallback || !adapter)
        {
            return;
        }

        auto statusMessage = adapter->wrapStatusData();
        if (statusMessage.empty())
        {
            return;
        }

        elink::BizEvent event;
        event.method = elink::MethodType::ON_PRINTER_EVENT_RAW;
        elink::PrinterEventRawData eventData;
        eventData.printerId = printerId;
        eventData.rawData = statusMessage.dump();
        event.data = eventData;
        eventCallback(event);
    }
}

namespace elink
{
    MqttService::MqttService()
    {
    }

    MqttService::~MqttService()
    {
        cleanup();
    }

    VoidResult MqttService::initialize(std::string caCertPath)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_initialized.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_IN_PROGRESS, "MQTT service is already initialized");
        }

        try
        {
            m_caCertPath = caCertPath;
            auto result = initializeClient("");
            if (!result.isSuccess())
            {
                return result;
            }

            setupCallbacks();
            m_initialized.store(true);
            ELEGOO_LOG_INFO("MQTT service initialization completed");
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT service initialization failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void MqttService::cleanup()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_initialized.load())
        {
            return;
        }

        try
        {
            cleanupClient();
            {
                std::lock_guard<std::mutex> callbackLock(m_eventCallbackMutex);
                m_eventCallback = nullptr;
            }
            {
                std::lock_guard<std::mutex> dataLock(m_dataMutex);
                m_printers.clear();
                m_messageAdapters.clear();
            }
            {
                std::lock_guard<std::mutex> connectionTimeLock(m_connectionTimeMutex);
                m_lastConnectedTime = std::nullopt;
            }
            m_initialized.store(false);
            ELEGOO_LOG_INFO("MQTT service cleanup completed");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred during MQTT service cleanup: {}", e.what());
        }
    }

    bool MqttService::isInitialized() const
    {
        return m_initialized.load();
    }

    VoidResult MqttService::connect(const MqttCredential &credential)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_mqttClient)
        {
            ELEGOO_LOG_WARN("MQTT client not initialized, cannot connect");
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "MQTT client not initialized");
        }

        ELEGOO_LOG_INFO("Connecting to MQTT broker: host={}, clientId={}, username={}",
                        credential.host, StringUtils::maskString(credential.mqttClientId), StringUtils::maskString(credential.mqttUserName));

        try
        {
            // Disconnect first if already connected
            m_mqttClient->disconnect();

            // Update configuration
            auto mqttConfig = m_mqttClient->getConfig();
            mqttConfig.brokerUrl = credential.host;
            mqttConfig.clientId = credential.mqttClientId;
            mqttConfig.username = credential.mqttUserName;
            mqttConfig.password = credential.mqttPassword;
            mqttConfig.caCertPath = m_caCertPath;
            m_mqttClient->updateConfig(mqttConfig);

            // Connect
            VoidResult result = m_mqttClient->connect();
            if (result.isSuccess())
            {
                ELEGOO_LOG_INFO("MQTT client connected successfully");
                subscribeToTopics(credential);
                return VoidResult::Success();
            }
            else
            {
                ELEGOO_LOG_ERROR("MQTT client connection failed: {}", result.message);
                return result;
            }
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT connection failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void MqttService::disconnect()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_mqttClient)
        {
            try
            {
                m_mqttClient->disconnect();
                ELEGOO_LOG_INFO("MQTT client disconnected");
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Error occurred while disconnecting MQTT client: {}", e.what());
            }
        }
    }

    bool MqttService::isConnected() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mqttClient && m_mqttClient->isConnected();
    }

    std::optional<std::chrono::system_clock::time_point> MqttService::getLastConnectedTime() const
    {
        std::lock_guard<std::mutex> lock(m_connectionTimeMutex);
        return m_lastConnectedTime;
    }

    void MqttService::setEventCallback(EventCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_eventCallbackMutex);
        m_eventCallback = callback;
    }

    void MqttService::updatePrinters(const std::vector<PrinterInfo> &printers)
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);

        m_printers = printers;
        // Note: Message adapters are now managed by NetworkService and are not created here
    }

    std::shared_ptr<IMessageAdapter> MqttService::getMessageAdapter(const std::string &printerId) const
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);

        auto it = m_messageAdapters.find(printerId);
        if (it != m_messageAdapters.end())
        {
            return it->second;
        }

        ELEGOO_LOG_WARN("Message adapter not found for printer: {}", StringUtils::maskString(printerId));
        return nullptr;
    }

    void MqttService::setMessageAdapter(const std::string &printerId, std::shared_ptr<IMessageAdapter> adapter)
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);

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

    VoidResult MqttService::initializeClient(const std::string &brokerUrl)
    {
        try
        {
            MqttConfig mqttConfig;
            mqttConfig.brokerUrl = brokerUrl;

            m_mqttClient = std::make_unique<MqttClient>(mqttConfig);
            ELEGOO_LOG_INFO("MQTT client initialized");
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT client initialization exception: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    void MqttService::cleanupClient()
    {
        if (m_mqttClient)
        {
            try
            {
                m_mqttClient->disconnect();
                m_mqttClient.reset();
                ELEGOO_LOG_INFO("MQTT client cleaned up");
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Error occurred while cleaning up MQTT client: {}", e.what());
            }
        }
    }

    void MqttService::setRawMessage(const std::string &printerId, const std::string &msg)
    {
    }

    void MqttService::setupCallbacks()
    {
        if (!m_mqttClient)
        {
            return;
        }

        // Set message callback
        m_mqttClient->setMessageCallback([this](const std::string &topic, const MqttMessage &message)
                                         {
            try 
            {
                // ELEGOO_LOG_DEBUG("Received MQTT message: topic={}, size={}", topic, message.payload.size());
                
                auto jsonData = nlohmann::json::parse(message.payload, nullptr, false);
                
                if (topic.find(TOPIC_DATA_SUFFIX) != std::string::npos) 
                {
                    auto printerSn = JsonUtils::safeGetString(jsonData, "deviceCode", "");
                    std::string printerId = getPrinterId(printerSn);
                    if (jsonData.contains("reportValue") && jsonData["reportValue"].is_string()) 
                    {
                        nlohmann::json data = nlohmann::json::parse(jsonData["reportValue"].get<std::string>(), nullptr, false);
                        // payload={"pkey":"SN","deviceCode":"SN","reportValue":"{\"extruder\":{\"temperature\":90},\"meta_data\":{\"id\":1769079268809}}"}
                        
                        // Check if a file upload is in progress for this printer
                        bool isUploading = false;
                        int uploadProgress = 0;
                        {
                            std::lock_guard<std::mutex> lock(m_dataMutex);
                            auto uploadIt = m_uploadStates.find(printerId);
                            if (uploadIt != m_uploadStates.end() && uploadIt->second.uploading)
                            {
                                isUploading = true;
                                uploadProgress = uploadIt->second.progress;
                                
                                // Merge incremental machine_status updates into cache
                                if (data.contains("machine_status") && data["machine_status"].is_object())
                                {
                                    if (uploadIt->second.cachedMachineStatus.is_null())
                                    {
                                        // First time: cache the complete status
                                        uploadIt->second.cachedMachineStatus = data["machine_status"];
                                        ELEGOO_LOG_DEBUG("Initial cache of machine_status for printer {}", StringUtils::maskString(printerId));
                                    }
                                    else
                                    {
                                        // Merge incremental updates into cached status
                                        uploadIt->second.cachedMachineStatus.update(data["machine_status"]);
                                        ELEGOO_LOG_DEBUG("Merged incremental machine_status update for printer {}", StringUtils::maskString(printerId));
                                    }
                                }
                            }
                        }

                        // Log original machine status only when not uploading
                        if (data.contains("machine_status") && data["machine_status"].is_object())
                        {
                            ELEGOO_LOG_INFO("[{}]Received machine status: {}", StringUtils::maskString(printerId), data.dump());
                            // If uploading, replace machine_status with simulated upload status
                            if (isUploading)
                            {
                                data["machine_status"] = {
                                    {"status", 11},       // File transferring state
                                    {"sub_status", 3000}, // File transfer sub-state
                                    {"progress", uploadProgress}
                                };
                            }
                        }      
                          
                        // Get adapter and callback function copies under lock protection
                        std::shared_ptr<IMessageAdapter> adapter;
                        EventCallback eventCallback = m_eventCallback;
                        {
                            std::lock_guard<std::mutex> lock(m_dataMutex);
                            auto it = m_messageAdapters.find(printerId);
                            if (it != m_messageAdapters.end()) 
                            {
                                adapter = it->second;
                            }
                        }
                    
                        // Process the status message outside the lock
                        if (adapter) 
                        {
                            nlohmann::json statusJson = adapter->wrapStatusData(data);
                                auto printerEvent = adapter->convertToEvent(statusJson.dump());
                            if (printerEvent.isValid() && eventCallback) 
                            {
                                {
                                    BizEvent event;
                                    event.method = printerEvent.method;
                                    event.data = printerEvent.data.value();
                                    eventCallback(event);
                                }

                                if(printerEvent.method == MethodType::ON_PRINTER_STATUS)
                                {
                                    if(adapter->isPrinterAttributesChanged())
                                    {
                                        // Update printer attributes cache
                                        auto  attributes = adapter->getPrinterAttributes();
                                        adapter->setPrinterAttributesChanged(false);
                                        BizEvent bizEvent;
                                        bizEvent.method = MethodType::ON_PRINTER_ATTRIBUTES;
                                        bizEvent.data = attributes;
                                        eventCallback(bizEvent);
                                    }
                                }

                                {
                                    emitRawPrinterEvent(eventCallback, printerId, adapter);
                                }
                                                              
                                // To ensure that the exception status is cleared in time, otherwise there may be a problem of continuously notifying the exception status
                                // Deprecated temporary solution for clearing exception status notifications
                                if(data.contains("machine_status") && data["machine_status"].is_object() &&
                                   data["machine_status"].contains("exception_status"))
                                {
                                    data["machine_status"] = {
                                        {"exception_status",std::vector<int>{} }
                                    };
                                    if(data.contains("meta_data"))
                                    {
                                        data.erase("meta_data");
                                    }
                                
                                    nlohmann::json clearExceptionJson = adapter->wrapStatusData(data);
                                    if (!clearExceptionJson.empty())
                                    {
                                        adapter->convertToEvent(clearExceptionJson.dump());
                                    }
                                }
                            }
                        }
                    }
                }
                else if (topic.find(TOPIC_CONNECTION_STATUS_SUFFIX) != std::string::npos) 
                {
                    if (jsonData.contains("deviceCode") && jsonData["deviceCode"].is_string() &&
                        jsonData.contains("onlineStatus") && jsonData["onlineStatus"].is_number_integer()) 
                    {
                        std::string printerSn = jsonData["deviceCode"];
                        std::string printerId = getPrinterId(printerSn);
                        int status = jsonData["onlineStatus"];
                        
                        ELEGOO_LOG_INFO("Received connection status for printer {}: {}",
                                        StringUtils::maskString(printerId), status == 1 ? "Online" : "Offline");
                        std::shared_ptr<IMessageAdapter> adapter;
                        {
                            std::lock_guard<std::mutex> lock(m_dataMutex);
                            auto it = m_messageAdapters.find(printerId);
                            if (it != m_messageAdapters.end()) 
                            {
                                adapter = it->second;
                            }
                        }

                        if(adapter)
                        {
                            adapter->setConnected(status == 1);
                        }
                    
                        EventCallback eventCallback = m_eventCallback;
                        emitConnectionStatusEvent(eventCallback,
                                                  printerId,
                                                  status == 1 ? ConnectionStatus::CONNECTED : ConnectionStatus::DISCONNECTED);

                        // If disconnected, also send printer status offline event
                        if(status == 0)
                        {
                            emitOfflinePrinterStatusEvent(eventCallback, printerId);
                            emitRawPrinterEvent(eventCallback, printerId, adapter);
                        }
                    }
                } else if (topic.find(TOPIC_EVENT_SUFFIX) != std::string::npos) {
                    // {"id":1758012402867,"deviceCode":"F01NZQQZJS2ASC8","data":{"eventType":"deviceBind"},"ack":0
                    if (jsonData.contains("deviceCode") && jsonData["deviceCode"].is_string() &&
                        jsonData.contains("data") && jsonData["data"].is_object()) 
                    {
                        std::string printerSn = jsonData["deviceCode"];
                         EventCallback eventCallback = m_eventCallback;
                            std::string printerId = getPrinterId(printerSn);
                        nlohmann::json eventDataJson = jsonData["data"];
                        if(eventDataJson.contains("eventType") && eventDataJson["eventType"].is_string()) {
                            std::string eventType = eventDataJson["eventType"];
                            if(eventType == "deviceBind") {
                                ELEGOO_LOG_INFO("Received deviceBind event for printer {}",
                                        StringUtils::maskString(printerId));
                                std::lock_guard<std::mutex> lock(m_dataMutex);
                                this->m_cacheBindResult[printerSn] = 1; // Bind successful
                                {
                                    BizEvent event;
                                    event.method = MethodType::ON_PRINTER_LIST_CHANGED;
                                    eventCallback(event);
                                }
                            }else if(eventType == "deviceUnbind") {
                                ELEGOO_LOG_INFO("Received deviceUnbind event for printer {}",
                                        StringUtils::maskString(printerId));
                                // std::lock_guard<std::mutex> lock(m_dataMutex);
                                // this->m_cacheBindResult[printerSn] = 2; // Unbind successful
                                emitOfflinePrinterStatusEvent(eventCallback, printerId);
                                if (eventCallback) 
                                {
                                    BizEvent event;
                                    event.method = MethodType::ON_PRINTER_LIST_CHANGED;
                                    eventCallback(event);
                                }
                            }else if(eventType == "deviceRejectBind") {  
                                ELEGOO_LOG_INFO("Received deviceRejectBind event for printer {}",
                                        StringUtils::maskString(printerId));
                                std::lock_guard<std::mutex> lock(m_dataMutex);
                                this->m_cacheBindResult[printerSn] = 2; // Bind rejected
                            }

                        }
                    }
                }
            } 
            catch (const std::exception& e) 
            {
                ELEGOO_LOG_ERROR("Error occurred while handling MQTT message callback: {}", e.what());
            } });

        // Set connection status callback
        m_mqttClient->setConnectionCallback([this](MqttConnectionState state, const std::string &message)
                                            {
            try 
            {               
                std::string stateStr;
                switch (state) 
                {
                    // Restore connection and refresh status
                    case MqttConnectionState::CONNECTED: stateStr = "Connected";
                    {
                        {
                            std::lock_guard<std::mutex> connectionTimeLock(m_connectionTimeMutex);
                            m_lastConnectedTime = std::chrono::system_clock::now();
                        }

                        // std::lock_guard<std::mutex> lock(m_dataMutex);
                        // for (const auto &[printerId, adapter] : m_messageAdapters)
                        // {
                        //     adapter->sendMessageToPrinter(MethodType::GET_PRINTER_STATUS, {});
                        // }
                        std::lock_guard<std::mutex> lock(m_dataMutex);
                        for (const auto &[printerId, adapter] : m_messageAdapters)
                        {
                            adapter->resetFullStatusLastUpdateTime();
                        }
                        break;
                    }
                    case MqttConnectionState::CONNECTING: stateStr = "Connecting"; break;
                    case MqttConnectionState::RECONNECTING: stateStr = "Reconnecting"; break;
                    case MqttConnectionState::DISCONNECTED: 
                    {
                        stateStr = "Disconnected"; 
                    }
                    case MqttConnectionState::CONNECTION_LOST: 
                    {
                        EventCallback eventCallback;
                        {
                            std::lock_guard<std::mutex> lock(m_eventCallbackMutex);
                            eventCallback = m_eventCallback;
                        }
                        if(stateStr.empty())
                        {
                            stateStr = "Connection Lost"; 
                        }
                        std::lock_guard<std::mutex> lock(m_dataMutex);
                        for (const auto &[printerId, adapter] : m_messageAdapters)
                        {
                            adapter->setConnected(false);

                            emitOfflinePrinterStatusEvent(eventCallback, printerId);
                            emitConnectionStatusEvent(eventCallback, printerId, ConnectionStatus::DISCONNECTED);
                            emitRawPrinterEvent(eventCallback, printerId, adapter);
                        }
                        break;
                    }
                    case MqttConnectionState::CONNECT_FAILED: stateStr = "Connection Failed"; break;
                }
                ELEGOO_LOG_INFO("MQTT connection status changed: {} - {}", stateStr, message);
            } 
            catch (const std::exception& e) 
            {
                ELEGOO_LOG_ERROR("Error occurred while handling MQTT connection status callback: {}", e.what());
            } });
    }

    void MqttService::subscribeToTopics(const MqttCredential &credential)
    {
        if (!m_mqttClient)
        {
            return;
        }

        std::string dataTopicPattern = std::string("app/v1/") + credential.mqttClientId + TOPIC_DATA_SUFFIX;
        std::string statusTopicPattern = std::string("app/v1/") + credential.mqttClientId + TOPIC_CONNECTION_STATUS_SUFFIX;
        std::string eventTopicPattern = std::string("app/v1/") + credential.mqttClientId + TOPIC_EVENT_SUFFIX;

        ELEGOO_LOG_INFO("Subscribing to MQTT topics: data={}, status={}, event={}", StringUtils::maskString(dataTopicPattern), StringUtils::maskString(statusTopicPattern), StringUtils::maskString(eventTopicPattern));

        auto dataSubResult = m_mqttClient->subscribe(dataTopicPattern);
        auto statusSubResult = m_mqttClient->subscribe(statusTopicPattern);
        auto eventSubResult = m_mqttClient->subscribe(eventTopicPattern);

        if (!dataSubResult.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to subscribe to data topic: {}", dataSubResult.message);
        }

        if (!statusSubResult.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to subscribe to status topic: {}", statusSubResult.message);
        }

        if (!eventSubResult.isSuccess())
        {
            ELEGOO_LOG_ERROR("Failed to subscribe to event topic: {}", eventSubResult.message);
        }
    }

    std::string MqttService::getPrinterId(const std::string &serialNumber) const
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        auto it = std::find_if(m_printers.begin(), m_printers.end(),
                               [&serialNumber](const PrinterInfo &p)
                               { return p.serialNumber == serialNumber; });
        if (it != m_printers.end())
        {
            return it->printerId;
        }
        return "";
    }

    void MqttService::setFileUploading(const std::string &printerId, bool uploading, int progress)
    {
        // Save cached machine_status before clearing upload state
        nlohmann::json cachedStatus;

        // Update upload status
        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            if (uploading)
            {
                m_uploadStates[printerId] = {true, progress};
                ELEGOO_LOG_INFO("Set printer {} uploading state: progress={}", StringUtils::maskString(printerId), progress);
            }
            else
            {
                // Save cached status before erasing
                auto uploadIt = m_uploadStates.find(printerId);
                if (uploadIt != m_uploadStates.end() && !uploadIt->second.cachedMachineStatus.is_null())
                {
                    cachedStatus = uploadIt->second.cachedMachineStatus;
                    ELEGOO_LOG_DEBUG("Restoring cached machine_status for printer {}", StringUtils::maskString(printerId));
                }

                m_uploadStates.erase(printerId);
                ELEGOO_LOG_INFO("Cleared printer {} uploading state", StringUtils::maskString(printerId));
            }
        }

        // Notify upload status change
        try
        {
            // Get adapter and callback function copies under lock protection
            std::shared_ptr<IMessageAdapter> adapter;
            EventCallback eventCallback;
            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                auto it = m_messageAdapters.find(printerId);
                if (it != m_messageAdapters.end())
                {
                    adapter = it->second;
                }
            }

            // Construct simulated upload status JSON
            nlohmann::json data;
            if (uploading)
            {
                data["machine_status"] = {
                    {"status", 11},       // 11 indicates uploading
                    {"sub_status", 3000}, // 3000 indicates file transfer
                    {"progress", progress}};
            }
            else
            {
                // Use cached machine_status if available, otherwise get from adapter
                if (!cachedStatus.is_null())
                {
                    data["machine_status"] = cachedStatus;
                }
                else
                {
                    auto status = adapter->getCachedFullStatusJson();
                    data["machine_status"] = status["machine_status"];
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_eventCallbackMutex);
                eventCallback = m_eventCallback;
            }

            // Process the status message outside the lock
            if (adapter && eventCallback)
            {
                nlohmann::json statusJson = adapter->wrapStatusData(data);
                auto printerEvent = adapter->convertToEvent(statusJson.dump());
                if (printerEvent.isValid())
                {
                    BizEvent event;
                    event.method = printerEvent.method;
                    event.data = printerEvent.data.value();
                    eventCallback(event);
                }

                {
                    emitRawPrinterEvent(eventCallback, printerId, adapter);
                }
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while notifying upload progress: {}", e.what());
        }
    }

    bool MqttService::isFileUploading(const std::string &printerId) const
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        auto it = m_uploadStates.find(printerId);
        return it != m_uploadStates.end() && it->second.uploading;
    }

    int MqttService::getUploadProgress(const std::string &printerId) const
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        auto it = m_uploadStates.find(printerId);
        return (it != m_uploadStates.end()) ? it->second.progress : 0;
    }
}
