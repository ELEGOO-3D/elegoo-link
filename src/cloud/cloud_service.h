#pragma once

#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <chrono>
#include "type.h"
#include "types/internal/internal.h"
#include "services/http_service.h"
#include "services/mqtt_service.h"
#include "services/rtm_service.h"
#include "adapters/elegoo_fdm_cc2_message_adapter.h"
#include <map>

namespace elink
{
    /**
     * Cloud Service Manager
     * Coordinates HTTP, MQTT, and RTM services for cloud-based printer control
     * Handles remote printer access through Elegoo cloud infrastructure
     */
    class CloudService
    {
    public:
        /**
         * Network configuration
         */
        struct NetworkConfig
        {
            NetworkConfig() {}
            std::string staticWebPath; // Static web files path
            std::string region;        // Region identifier, e.g., "us", "cn"
            std::string baseApiUrl;    // Base API URL, e.g. "https://api.elegoo.com", if empty, will use default
            std::string userAgent;     // User-Agent string
            std::string caCertPath;    // CA certificate path for SSL/TLS verification
        };

        using FileUploadProgressCallback = std::function<bool(const FileUploadProgressData &progress)>;

    public:
        CloudService();
        ~CloudService();

        // Set callback
        void setEventCallback(EventCallback callback);

        // Initialize and cleanup
        VoidResult initialize(const NetworkConfig &config);
        void cleanup();
        bool isInitialized() const;

        VoidResult setRegion(const SetRegionParams &params);

        GetUserInfoResult getUserInfo(const GetUserInfoParams &params);
        // Authentication management
        VoidResult setHttpCredential(const HttpCredential &credential);

        BizResult<HttpCredential> getHttpCredential() const;

        BizResult<HttpCredential> refreshHttpCredential(const HttpCredential &credential);

        VoidResult clearHttpCredential();

        VoidResult logout();

        // API calls
        GetRtcTokenResult getRtcToken() const;

        GetPrinterListResult getPrinters();

        VoidResult sendRtmMessage(const SendRtmMessageParams &params);

        BindPrinterResult bindPrinter(const BindPrinterParams &params);

        VoidResult cancelBindPrinter(const CancelBindPrinterParams &params);

        VoidResult unbindPrinter(const UnbindPrinterParams &params);

        ConnectPrinterResult connectPrinter(const ConnectPrinterParams &params);

        DisconnectPrinterResult disconnectPrinter(const DisconnectPrinterParams &params);

        GetFileListResult getFileList(const GetFileListParams &params);

        GetFileDetailResult getFileDetail(const GetFileDetailParams &params);

        PrintTaskListResult getPrintTaskList(const PrintTaskListParams &params);

        DeletePrintTasksResult deletePrintTasks(const DeletePrintTasksParams &params);

        StartPrintResult startPrint(const StartPrintParams &params);
        VoidResult stopPrint(const StopPrintParams &params);
        VoidResult pausePrint(const PausePrintParams &params);
        VoidResult resumePrint(const ResumePrintParams &params);

        GetCanvasStatusResult getCanvasStatus(const GetCanvasStatusParams &params);

        SetAutoRefillResult setAutoRefill(const SetAutoRefillParams &params);

        PrinterStatusResult getPrinterStatus(const PrinterStatusParams &params);

        PrinterAttributesResult getPrinterAttributes(const PrinterAttributesParams &params);

        /**
         * Refresh printer attributes, The result will be notified through events
         * @param params Printer attributes parameters
         * @return Operation result
         */
        VoidResult refreshPrinterAttributes(const PrinterAttributesParams &params);

        /**
         * Refresh printer status, The result will be notified through events
         * @param params Printer status parameters
         * @return Operation result
         */
        VoidResult refreshPrinterStatus(const PrinterStatusParams &params);

        PrinterStatusResult getPrinterStatusFromHttp(const PrinterStatusParams &params);

        // Get device status raw data
        BizResult<std::string> getPrinterStatusRaw(const PrinterStatusParams &params);

        FileUploadResult uploadFile(
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback = nullptr);

        /**
         * Cancel file upload
         * @param params Cancel parameters (contains printerId)
         * @return Operation result
         */
        VoidResult cancelFileUpload(const CancelFileUploadParams &params);

        VoidResult updatePrinterName(const UpdatePrinterNameParams &params);

        /**
         * Get list of devices with expired Agora license
         * @return Result containing list of expired devices
         */
        GetLicenseExpiredDevicesResult getLicenseExpiredDevices();

        /**
         * Renew Agora license for a device
         * @param params Renew parameters (serialNumber is required)
         * @return Operation result
         */
        RenewLicenseResult renewLicense(const RenewLicenseParams &params);

        std::vector<PrinterInfo> getCachedPrinters() const
        {
            std::shared_lock lock(m_printersMutex);
            return m_printers;
        }

        HttpService *getHttpService()
        {
            std::shared_lock lock(m_servicesMutex);
            return m_httpService.get();
        }

    private:
        std::string getSerialNumberByPrinterId(const std::string &printerId) const
        {
            auto it = std::find_if(m_printers.begin(), m_printers.end(),
                                   [&printerId](const PrinterInfo &p)
                                   { return p.printerId == printerId; });
            if (it != m_printers.end())
            {
                return it->serialNumber;
            }
            return "";
        }

    private:
        // Background task management
        void startBackgroundTasks();
        void stopBackgroundTasks();
        void connectionMonitorTask();

        // Credential management
        void refreshCredentials();
        // void connectServices();

        // Retry logic
        void retryConnections();

        // Message adapter management
        void createMessageAdapters();
        void updateServicesWithAdapters();

        // Service state check helper methods
        VoidResult validateHttpServiceState() const;
        VoidResult validateRtmServiceState() const;

        void setOnlineStatus(bool isOnline);

    private:
        // Service instances
        std::unique_ptr<HttpService> m_httpService;
        std::unique_ptr<MqttService> m_mqttService;
        std::unique_ptr<RtmService> m_rtmService;

        // Credential cache - protected by shared pointer and lock
        std::shared_ptr<const AgoraCredential> m_agoraCredential{nullptr};
        std::shared_ptr<const MqttCredential> m_mqttCredential{nullptr};
        mutable std::shared_mutex m_credentialsMutex; // Protect credential data

        // Store historical HttpCredentials
        std::vector<HttpCredential> m_credentialHistory;

        // Callback
        EventCallback m_eventCallback{nullptr};
        mutable std::mutex m_callbackMutex; // Protect callback function

        // State
        std::atomic<bool> m_initialized{false};

        // Fine-grained lock design
        mutable std::shared_mutex m_servicesMutex; // Protect service instances
        mutable std::shared_mutex m_printersMutex; // Protect printer-related data
        mutable std::mutex m_configMutex;          // Protect configuration data

        NetworkConfig m_networkConfig;

        // std::vector<PrinterInfo> m_serverPrinters; // Printer list from server
        std::vector<PrinterInfo> m_printers; // Local cached printer list
        std::map<std::string, std::shared_ptr<IMessageAdapter>> m_messageAdapters;

        // Background task related
        std::atomic<bool> m_backgroundTasksRunning{false};
        std::thread m_connectionMonitorThread;
        std::condition_variable m_backgroundTasksCv;
        std::mutex m_backgroundTasksMutex;
        // Used to trigger immediate wake-up of background tasks (e.g., credential updates)
        std::atomic<bool> m_backgroundTasksWakeRequested{false};

        // Configuration constants
        static constexpr int TOKEN_REFRESH_CHECK_INTERVAL_SECONDS = 300; // Check every 5 minutes
        static constexpr int CONNECTION_MONITOR_INTERVAL_SECONDS = 10;   // Check connection every 10 seconds

        bool m_IsRefreshingCredentials = false;
        std::mutex m_refreshCredentialsMutex;

        ELINK_ERROR_CODE m_lastHttpErrorCode = ELINK_ERROR_CODE::SUCCESS;

        // File upload status tracking
        std::map<std::string, bool> m_uploadingFiles; // key: printerId, value: is uploading
        mutable std::mutex m_uploadingFilesMutex;     // Protect upload status

        // File upload cancellation tracking
        std::map<std::string, bool> m_uploadCancellations; // key: printerId, value: is cancelled
        mutable std::mutex m_uploadCancellationsMutex;     // Protect upload cancellation status
        // Bind printer state tracking
        enum class BindState
        {
            Idle,     // Not binding
            Binding,  // Binding in progress
            Cancelled // Binding cancelled
        };
        std::map<std::string, BindState> m_bindStates; // key: serialNumber, value: bind state
        mutable std::mutex m_bindStatesMutex;          // Protect bind states

        bool m_isOnline = false;

        // Add member variables for caching
        SetRegionParams m_cachedRegionParams;
        HttpCredential m_cachedHttpCredential;
    };

    /**
     * Get global cloud service manager instance
     */
    CloudService &getCloudService();
}
