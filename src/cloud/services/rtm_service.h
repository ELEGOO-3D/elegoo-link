#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <future>
#include "type.h"
#include "protocols/rtm_client.h"
#include "adapters/elegoo_fdm_cc2_message_adapter.h"
#include "utils/process_mutex.h"
#include "utils/logger.h"
#include <map>

namespace elink
{
    struct DownloadFileStatus
    {
        std::string printerId; // Printer ID
        std::string taskId;    // Task ID
        int status;            // Status: 0-In progress, 1-Ended, 2-Cancelled, 3-Abnormal interruption
        int progress;          // Progress 0-100
        std::chrono::steady_clock::time_point lastUpdatedTime;
    };
    /**
     * RTM service manager
     * Responsible for Agora RTM connection, message subscription and publishing
     */
    class RtmService
    {
    public:
        RtmService();
        ~RtmService();

        // Initialization and cleanup
        VoidResult initialize();
        void cleanup();
        bool isInitialized() const;

        // Connection management
        VoidResult connect(const AgoraCredential &credential);
        void disconnect();
        bool isConnected() const;

        bool isLoginOtherDevice() const;

        /**
         * Clear the login from other device state
         * Should be called when user sets new credentials to allow reconnection
         */
        void clearLoginOtherDeviceState();
        // Message sending
        VoidResult sendMessage(const SendRtmMessageParams &params);

        /**
         * High-level request execution with validation and type conversion
         * @tparam ResponseType Expected response type
         * @param request Request to execute
         * @param actionName Action name for logging
         * @param timeout Request timeout
         * @return BizResult<ResponseType> with converted response
         */
        template <typename ResponseType>
        BizResult<ResponseType> executeRequest(
            const BizRequest &request,
            const std::string &actionName,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(10000), bool logRequest = true)
        {
            if (logRequest)
            {
                ELEGOO_LOG_DEBUG("[RTM] Executing {}", actionName);
            }
            
            // Validate initialized state
            if (!m_initialized.load())
            {
                return BizResult<ResponseType>{ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM service not initialized"};
            }

            // Execute request and get JSON result
            auto jsonResult = sendRequest(request, timeout);

            // Convert result to the expected ResponseType
            BizResult<ResponseType> response;
            response.code = jsonResult.code;
            response.message = jsonResult.message;

            // Handle response data conversion
            if constexpr (std::is_same_v<ResponseType, nlohmann::json>)
            {
                response.data = jsonResult.data;
            }
            else if constexpr (std::is_same_v<ResponseType, std::monostate>)
            {
                // For VoidResult, don't set data
            }
            else
            {
                // For other types, try to convert from json
                if (jsonResult.data.has_value())
                {
                    try
                    {
                        response.data = jsonResult.data.value().get<ResponseType>();
                    }
                    catch (const std::exception &e)
                    {
                        ELEGOO_LOG_WARN("Failed to convert response data: {}", e.what());
                        response.code = ELINK_ERROR_CODE::UNKNOWN_ERROR;
                        response.message = "Failed to convert response data";
                    }
                }
            }

            if (response.code == ELINK_ERROR_CODE::SUCCESS)
            {
                if (logRequest)
                {
                    ELEGOO_LOG_DEBUG("[RTM] {} succeeded",  actionName);
                }
            }
            else
            {
                if (logRequest)
                {
                    ELEGOO_LOG_ERROR("[RTM] {} failed: {}", actionName, response.message);
                }
            }

            return response;
        }

        // Message callback
        void setEventCallback(EventCallback callback);

        // Connection state callback type definition
        using ConnectionStateCallback = std::function<void(bool isConnected, RtmConnectionState state, RtmConnectionChangeReason reason)>;

        /**
         * Set connection state callback
         * @param callback Callback function, parameters: isConnected (whether connected), state (connection state), reason (state change reason)
         */
        void setConnectionStateCallback(ConnectionStateCallback callback);

        // Printer management
        void updatePrinters(const std::vector<PrinterInfo> &printers);

        // Message adapter management
        std::shared_ptr<IMessageAdapter> getMessageAdapter(const std::string &printerId) const;
        void setMessageAdapter(const std::string &printerId, std::shared_ptr<IMessageAdapter> adapter);

        int getBindResult(const std::string &printerId)
        {
            std::lock_guard<std::mutex> lock(m_bindResultMutex);
            auto it = m_cacheBindResult.find(printerId);
            if (it != m_cacheBindResult.end())
            {
                return it->second;
            }
            return -1; // Not found
        }
        void clearBindResult(const std::string &printerId)
        {
            std::lock_guard<std::mutex> lock(m_bindResultMutex);
            m_cacheBindResult.erase(printerId);
        }

        DownloadFileStatus getDownloadFileStatus(const std::string &printerId)
        {
            std::lock_guard<std::mutex> lock(m_downloadFileStatusMutex);
            auto it = m_cacheDownloadFileStatus.find(printerId);
            if (it != m_cacheDownloadFileStatus.end())
            {
                return it->second;
            }
            return DownloadFileStatus{}; // Not found
        }

        void resetDownloadFileStatus(const std::string &printerId)
        {
            std::lock_guard<std::mutex> lock(m_downloadFileStatusMutex);
            DownloadFileStatus status;
            status.printerId = printerId;
            status.taskId = "";
            status.status = 0;
            status.progress = 0;
            status.lastUpdatedTime = std::chrono::steady_clock::now();
            m_cacheDownloadFileStatus[printerId] = status;
        }

    private:
        // Internal methods
        VoidResult initializeClient();
        void cleanupClient();
        void setupCallbacks();
        void subscribeToChannels(const AgoraCredential &credential);
        std::unique_ptr<RtmClient> createRtmClient(const RtmConfig &config);
        void handleResponseMessage(const std::string &requestId, ELINK_ERROR_CODE code, std::string message, const std::optional<nlohmann::json> &result);
        void handleEventMessage(const BizEvent &event);

        void onRtmMessageReceived(const RtmMessage &message);
        
        /**
         * Internal synchronous request method
         * @param request Request message
         * @param timeout Custom timeout (in milliseconds), uses default 10s timeout if set to 0
         * @return BizResult containing the response or error
         */
        BizResult<nlohmann::json> sendRequest(const BizRequest &request,
                                              std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    private:
        // Request management
        struct PendingRequest
        {
            std::string requestId;
            std::shared_ptr<std::promise<BizResult<nlohmann::json>>> promise;
            MethodType requestType; // Request type for logging/debugging
        };

        std::map<std::string, PendingRequest> pendingRequests_;
        std::mutex requestsMutex_;
        std::string getPrinterIdBySerialNumber(const std::string &serialNumber) const
        {
            std::lock_guard<std::mutex> lock(m_printersMutex);
            auto it = std::find_if(m_printers.begin(), m_printers.end(),
                                   [&serialNumber](const PrinterInfo &p)
                                   { return p.serialNumber == serialNumber; });
            if (it != m_printers.end())
            {
                return it->printerId;
            }
            return "";
        }

        std::string getSerialNumberByPrinterId(const std::string &printerId) const
        {
            std::lock_guard<std::mutex> lock(m_printersMutex);
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
        // RTM client - protected by independent lock
        std::unique_ptr<RtmClient> m_rtmClient;
        mutable std::mutex m_clientMutex;

        // Callbacks - protected by independent lock
        EventCallback m_eventCallback;
        ConnectionStateCallback m_connectionStateCallback;
        mutable std::mutex m_eventCallbackMutex;
        mutable std::mutex m_connectionStateCallbackMutex;

        // State
        std::atomic<bool> m_initialized{false};
        std::atomic<bool> m_isLoginOtherDevice{false}; // Track if user is logged in from another device

        // Printer information and message adapters - protected by independent lock
        std::vector<PrinterInfo> m_printers;
        std::map<std::string, std::shared_ptr<IMessageAdapter>> m_messageAdapters;
        mutable std::mutex m_printersMutex;

        // Cached credential information - protected by independent lock
        AgoraCredential m_cachedCredential;
        mutable std::mutex m_credentialMutex;

        // RTM constants
        static constexpr int SUBSCRIBE_DELAY_MS = 5000; // Subscription interval delay

        // Binding result cache - protected by independent lock
        std::map<std::string, int> m_cacheBindResult;
        mutable std::mutex m_bindResultMutex;

        mutable std::mutex m_downloadFileStatusMutex;
        std::map<std::string, DownloadFileStatus> m_cacheDownloadFileStatus;
    };
}
