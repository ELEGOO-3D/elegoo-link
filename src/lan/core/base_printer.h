#pragma once

#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <future>
#include "type.h"
#include "types/internal/internal.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "types/internal/json_serializer.h"
namespace elink
{
    // Forward declarations
    template <typename T>
    struct PrinterBizRequest;
    class IMessageAdapter;
    class IProtocol;
    class PrinterManager;
    class IHttpFileTransfer;

    /**
     * BasePrinter - Abstract base class for all printers
     * Provides common functionality for printer operations
     * Subclasses should implement printer-specific behaviors
     */
    class BasePrinter
    {
    public:
        /**
         * Constructor
         * @param printerInfo Printer information
         */
        explicit BasePrinter(const PrinterInfo &printerInfo);

        /**
         * Virtual destructor
         */
        virtual ~BasePrinter();

        /**
         * Initialize the printer (must be called after construction)
         * Creates protocol, message adapter, and file uploader
         * @throws std::runtime_error if initialization fails
         */
        void initialize();

        // Disable copy constructor and assignment
        BasePrinter(const BasePrinter &) = delete;
        BasePrinter &operator=(const BasePrinter &) = delete;

        // ========== Basic Information ==========

        /**
         * Get printer ID
         */
        const std::string &getId() const { return printerInfo_.printerId; }

        /**
         * Get printer information
         */
        const PrinterInfo &getPrinterInfo() const { return printerInfo_; }

        // ========== Connection Management ==========

        /**
         * Connect to the printer
         * @return BizResult containing the connection result
         */
        BizResult<nlohmann::json> connect(const ConnectPrinterParams &params = ConnectPrinterParams());

        /**
         * Disconnect the printer
         * @return BizResult containing the disconnection result
         */
        BizResult<nlohmann::json> disconnect();

        /**
         * Check if connected
         */
        bool isConnected() const;

        /**
         * Get connection status
         */
        ConnectionStatus getConnectionStatus() const;

        // ========== Printer Control ==========

        /**
         * Synchronous request interface
         * @param request Request message
         * @param timeout Custom timeout (milliseconds), 0 means use default timeout
         * @return BizResult<nlohmann::json> containing the response or error
         */
        BizResult<nlohmann::json> request(
            const BizRequest &request,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

        // ========== Callback Settings ==========

        /**
         * Set event callback - Only for printer-initiated status and event events
         * @param callback Event callback function
         */
        void setEventCallback(std::function<void(const BizEvent &)> callback);

        /**
         * Get file uploader
         */
        std::shared_ptr<IHttpFileTransfer> getFileUploader() const
        {
            return fileUploader_;
        }

        // ========== Print Control Methods ==========

        /**
         * Start print job
         * @param params Start print parameters
         * @return Operation result
         */
        virtual VoidResult startPrint(const StartPrintParams &params);

        /**
         * Pause current print job
         * @param params Printer base parameters
         * @return Operation result
         */
        virtual VoidResult pausePrint(const PrinterBaseParams &params);

        /**
         * Resume paused print job
         * @param params Printer base parameters
         * @return Operation result
         */
        virtual VoidResult resumePrint(const PrinterBaseParams &params);

        /**
         * Stop current print job
         * @param params Printer base parameters
         * @return Operation result
         */
        virtual VoidResult stopPrint(const PrinterBaseParams &params);

        /**
         * Set auto refill settings
         * @param params Auto refill parameters
         * @return Operation result
         */
        virtual VoidResult setAutoRefill(const SetAutoRefillParams &params);

        /**
         * Get printer attributes
         * @param params Printer attributes parameters
         * @param timeout Request timeout in milliseconds
         * @return PrinterAttributesResult containing printer attributes data
         */
        virtual PrinterAttributesResult getPrinterAttributes(const PrinterAttributesParams &params, int timeout = 3000);

        /**
         * Get printer status
         * @param params Printer status parameters
         * @param timeout Request timeout in milliseconds
         * @return PrinterStatusResult containing printer status data
         */
        virtual PrinterStatusResult getPrinterStatus(const PrinterStatusParams &params, int timeout = 3000);

        /**
         * Get canvas status
         * @param params Canvas status parameters
         * @return GetCanvasStatusResult containing canvas status data
         */
        virtual GetCanvasStatusResult getCanvasStatus(const GetCanvasStatusParams &params);

        /**
         * Update printer name
         * @param params Update printer name parameters
         * @return Operation result
         */
        virtual VoidResult updatePrinterName(const UpdatePrinterNameParams &params);

    protected:
        // ========== Virtual Methods for Subclass Customization ==========

        /**
         * Called after successful connection
         * Subclasses can override to perform printer-specific initialization
         */
        virtual void onConnected(const ConnectPrinterParams &params);

        /**
         * Called before disconnection
         * Subclasses can override to perform printer-specific cleanup
         */
        virtual void onDisconnecting();

        /**
         * Process printer-specific request validation
         * @return true if request is valid, false otherwise
         */
        virtual bool validateRequest(const BizRequest &request);

        /**
         * Get printer-specific default timeout
         * @return Default timeout in milliseconds
         */
        virtual std::chrono::milliseconds getDefaultTimeout() const;

        /**
         * Create protocol instance for this printer
         * Subclasses should override to create printer-specific protocol
         * @return Protocol instance
         */
        virtual std::unique_ptr<IProtocol> createProtocol();

        /**
         * Create message adapter for this printer
         * Subclasses should override to create printer-specific message adapter
         * @return Message adapter instance
         */
        virtual std::unique_ptr<IMessageAdapter> createMessageAdapter();

        /**
         * Create file uploader for this printer
         * Subclasses should override to create printer-specific file uploader
         * @return File uploader instance, or nullptr if not supported
         */
        virtual std::unique_ptr<IHttpFileTransfer> createFileUploader();

        // ========== Protected Helper Methods ==========

        /**
         * Handle incoming message from protocol
         */
        void onMessage(const std::string &messageData);

        /**
         * Handle protocol status change
         */
        virtual void onProtocolStatusChanged(bool connected);

        /**
         * Send request to printer via protocol
         */
        void sendPrinterRequest(const PrinterBizRequest<std::string> &request);

        /**
         * Handle response message from printer
         */
        void handleResponseMessage(
            const std::string &requestId,
            ELINK_ERROR_CODE code,
            std::string message,
            const std::optional<nlohmann::json> &result);

        /**
         * Handle event message from printer
         */
        void handleEventMessage(const BizEvent &event);

        /**
         * Cleanup pending requests
         */
        void cleanupPendingRequests(const std::string &reason);

        /**
         * Handle request with timeout
         */
        BizResult<nlohmann::json> handleRequest(
            const BizRequest &request,
            std::chrono::milliseconds timeout);

        /**
         * Register a pending request
         */
        std::shared_ptr<std::promise<BizResult<nlohmann::json>>> registerPendingRequest(
            const std::string &requestId);

        /**
         * Execute typed request with automatic conversion
         * @tparam ResponseType Expected response type
         * @param method Request method type
         * @param params Request parameters
         * @param actionName Action name for logging
         * @param timeout Request timeout
         * @return BizResult<ResponseType> with converted response
         */
        template <typename ResponseType, typename ParamsType>
        BizResult<ResponseType> executeRequest(
            MethodType method,
            const ParamsType &params,
            const std::string &actionName,
            std::chrono::milliseconds timeout)
        {
            ELEGOO_LOG_INFO("[{}] {}", StringUtils::maskString(printerInfo_.printerId), actionName);
            
            BizRequest request;
            request.method = method;
            request.params = nlohmann::json(params);
            
            auto result = handleRequest(request, timeout);
            
            BizResult<ResponseType> response;
            response.code = result.code;
            response.message = result.message;
            
            // Handle response data conversion
            if constexpr (std::is_same_v<ResponseType, nlohmann::json>)
            {
                response.data = result.data;
            }
            else if constexpr (std::is_same_v<ResponseType, std::monostate>)
            {
                // For VoidResult, don't set data
            }
            else
            {
                // For other types, try to convert from json
                if (result.data.has_value())
                {
                    try
                    {
                        response.data = result.data.value().get<ResponseType>();
                    }
                    catch (const std::exception &e)
                    {
                        ELEGOO_LOG_WARN("Failed to convert response data for {}: {}", actionName, e.what());
                    }
                }
            }
            
            return response;
        }

    protected:
        // Request management
        struct PendingRequest
        {
            std::string requestId;
            std::shared_ptr<std::promise<BizResult<nlohmann::json>>> promise;
            std::chrono::steady_clock::time_point timestamp;
        };

        std::map<std::string, PendingRequest> pendingRequests_;
        std::mutex requestsMutex_;

        // Printer information and components
        PrinterInfo printerInfo_;
        std::shared_ptr<IProtocol> protocol_;
        std::unique_ptr<IMessageAdapter> adapter_;
        std::shared_ptr<IHttpFileTransfer> fileUploader_;

        // Connection status
        std::atomic<bool> isConnected_;
        ConnectionStatus connectionStatus_;
        mutable std::mutex statusMutex_;

        // Event callback
        std::function<void(const BizEvent &)> eventCallback_;
        std::mutex callbackMutex_;

        std::string protocolType_; // Protocol type for logging

        // Status polling thread management
        std::atomic<bool> statusPollingRunning_;
        std::thread statusPollingThread_;
        std::mutex statusPollingMutex_;
        std::condition_variable statusPollingCV_;
        
        /**
         * Start status polling thread
         * Polls printer status until first successful response
         */
        void startStatusPolling();
        
        /**
         * Stop status polling thread
         */
        void stopStatusPolling();
        
        /**
         * Status polling thread function
         */
        void statusPollingThreadFunc();
    };

    /**
     * Printer smart pointer type definition
     */
    using PrinterPtr = std::shared_ptr<BasePrinter>;

} // namespace elink
