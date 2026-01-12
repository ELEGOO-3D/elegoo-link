#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include "type.h"
#include "protocols/mqtt_client.h"
#include "adapters/elegoo_fdm_cc2_message_adapter.h"
#include "utils/process_mutex.h"
#include <map>

namespace elink
{
    /**
     * MQTT service manager
     * Responsible for MQTT connection, message subscription and publishing
     */
    class MqttService
    {
    public:
        MqttService();
        ~MqttService();

        // Initialization and cleanup
        VoidResult initialize(std::string caCertPath);
        void cleanup();
        bool isInitialized() const;

        // Connection management
        VoidResult connect(const MqttCredential &credential);
        void disconnect();
        bool isConnected() const;

        // Message callback
        void setEventCallback(EventCallback callback);

        // Printer management
        void updatePrinters(const std::vector<PrinterInfo> &printers);

        // Message adapter management
        std::shared_ptr<IMessageAdapter> getMessageAdapter(const std::string &printerId) const;
        void setMessageAdapter(const std::string &printerId, std::shared_ptr<IMessageAdapter> adapter);

        void setRawMessage(const std::string &printerId, const std::string &msg);

        int getBindResult(const std::string &printerId)
        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            auto it = m_cacheBindResult.find(printerId);
            if (it != m_cacheBindResult.end())
            {
                return it->second;
            }
            return -1; // Not found
        }

        void clearBindResult(const std::string &printerId)
        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            m_cacheBindResult.erase(printerId);
        }

        // File upload status management
        void setFileUploading(const std::string &printerId, bool uploading, int progress = 0);
        bool isFileUploading(const std::string &printerId) const;
        int getUploadProgress(const std::string &printerId) const;

        void setCaCertPath(const std::string &path)
        {
            m_caCertPath = path;
        }
    private:
        // Internal methods
        VoidResult initializeClient(const std::string &brokerUrl);
        void cleanupClient();
        void setupCallbacks();
        void subscribeToTopics(const MqttCredential &credential);

        std::string getPrinterId(const std::string &serialNumber) const;
    private:
        // MQTT client
        std::unique_ptr<MqttClient> m_mqttClient;

        std::mutex m_eventCallbackMutex;
        // Callbacks
        EventCallback m_eventCallback;

        // State
        std::atomic<bool> m_initialized{false};
        mutable std::mutex m_mutex;

        mutable std::mutex m_dataMutex;
        // Printer information and message adapters
        std::vector<PrinterInfo> m_printers;
        std::map<std::string, std::shared_ptr<IMessageAdapter>> m_messageAdapters;

        // Topic suffix constants
        static constexpr const char *TOPIC_DATA_SUFFIX = "/device/data";
        static constexpr const char *TOPIC_CONNECTION_STATUS_SUFFIX = "/device/onoffline";
        static constexpr const char *TOPIC_EVENT_SUFFIX = "/event";

        std::map<std::string, int> m_cacheBindResult;
        
        // File upload status tracking
        struct UploadState {
            bool uploading = false;
            int progress = 0;
        };
        std::map<std::string, UploadState> m_uploadStates; // key: printerId


        std::string m_caCertPath;
    };
}
