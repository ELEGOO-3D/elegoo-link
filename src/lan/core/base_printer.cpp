#include "core/base_printer.h"
#include "protocols/protocol_interface.h"
#include "protocols/message_adapter.h"
#include "protocols/file_transfer.h"
#include "types/base.h"
#include "types/printer.h"
#include "utils/utils.h"
#include "utils/logger.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>
#include <sstream>
#include <future>
#include "types/internal/internal.h"
#include "types/internal/json_serializer.h"

namespace elink
{
    BasePrinter::BasePrinter(const PrinterInfo &printerInfo)
        : printerInfo_(printerInfo),
          isConnected_(false),
          connectionStatus_(ConnectionStatus::DISCONNECTED),
          statusPollingRunning_(false)
    {
        ELEGOO_LOG_INFO("Creating printer {} (Type: {})",
                        StringUtils::maskString(printerInfo_.printerId),
                        printerTypeToString(printerInfo_.printerType));
    }

    void BasePrinter::initialize()
    {
        ELEGOO_LOG_INFO("Initializing printer {} (Type: {})",
                        StringUtils::maskString(printerInfo_.printerId),
                        printerTypeToString(printerInfo_.printerType));

        // Create protocol and adapter using virtual methods
        protocol_ = createProtocol();
        adapter_ = createMessageAdapter();
        fileUploader_ = createFileUploader();

        if (!protocol_)
        {
            std::string error = "Failed to create protocol for printer type: " +
                                printerTypeToString(printerInfo_.printerType);
            ELEGOO_LOG_ERROR("{}", error);
            throw std::runtime_error(error);
        }

        if (!adapter_)
        {
            std::string error = "Failed to create message adapter for printer type: " +
                                printerTypeToString(printerInfo_.printerType);
            ELEGOO_LOG_ERROR("{}", error);
            throw std::runtime_error(error);
        }

        // Get protocol type for logging
        protocolType_ = protocol_->getProtocolType();

        // Set protocol callbacks
        protocol_->setConnectStatusCallback([this](bool connected)
                                            { onProtocolStatusChanged(connected); });

        protocol_->setMessageCallback([this](const std::string &messageData)
                                      { onMessage(messageData); });

        if (!fileUploader_)
        {
            ELEGOO_LOG_WARN("File uploader not available for printer {} (type: {})",
                            StringUtils::maskString(printerInfo_.printerId),
                            printerTypeToString(printerInfo_.printerType));
        }

        // Set message sending callback
        if (adapter_)
        {
            adapter_->setMessageSendCallback([this](const PrinterBizRequest<std::string> &request)
                                             { this->sendPrinterRequest(request); });
            ELEGOO_LOG_DEBUG("Message send callback set for printer {}",
                             StringUtils::maskString(printerInfo_.printerId));
        }

        ELEGOO_LOG_INFO("Printer {} initialized successfully",
                        StringUtils::maskString(printerInfo_.printerId));
    }

    BasePrinter::~BasePrinter()
    {
        if (adapter_)
        {
            adapter_->setMessageSendCallback(nullptr);
        }

        if (protocol_)
        {
            protocol_->setMessageCallback(nullptr);
            protocol_->setConnectStatusCallback(nullptr);
        }

        (void)disconnect();

        cleanupPendingRequests("Printer destroyed");
        // Stop status polling thread first
        stopStatusPolling();
        ELEGOO_LOG_INFO("Printer {} destroyed", StringUtils::maskString(printerInfo_.printerId));
    }

    // ========== Connection Management ==========

    BizResult<nlohmann::json> BasePrinter::connect(const ConnectPrinterParams &params)
    {
        if (isConnected_)
        {
            ELEGOO_LOG_INFO("Printer {} is already connected",
                            StringUtils::maskString(printerInfo_.printerId));
            return BizResult<nlohmann::json>::Success();
        }

        std::lock_guard<std::mutex> lock(statusMutex_);

        try
        {
            // Check if protocol is initialized
            if (!protocol_)
            {
                std::string detailedError = "Protocol not initialized for printer type: " +
                                            printerTypeToString(printerInfo_.printerType);
                ELEGOO_LOG_ERROR("{}", detailedError);
                return BizResult<nlohmann::json>{ELINK_ERROR_CODE::UNKNOWN_ERROR, detailedError};
            }

            // Check if adapter is initialized
            if (!adapter_)
            {
                std::string detailedError = "Message adapter not initialized for printer type: " +
                                            printerTypeToString(printerInfo_.printerType);
                ELEGOO_LOG_ERROR("{}", detailedError);
                return BizResult<nlohmann::json>{ELINK_ERROR_CODE::UNKNOWN_ERROR, detailedError};
            }

            // Check printer info integrity
            std::string printerInfoError;
            if (printerInfo_.printerId.empty())
            {
                printerInfoError = "Printer ID is empty";
            }
            else if (params.host.empty())
            {
                printerInfoError = "Host is empty";
            }
            if (!printerInfoError.empty())
            {
                ELEGOO_LOG_ERROR("Invalid printer info for printer {}: {}",
                                 StringUtils::maskString(printerInfo_.printerId), printerInfoError);
                return BizResult<nlohmann::json>{
                    ELINK_ERROR_CODE::INVALID_PARAMETER, "Invalid printer info: " + printerInfoError};
            }

            // Try to connect protocol
            ELEGOO_LOG_INFO("Attempting to connect to printer {} at {}",
                            StringUtils::maskString(printerInfo_.printerId),
                            params.host);

            VoidResult connectionResult = protocol_->connect(params, params.autoReconnect);

            if (connectionResult.isError())
            {
                connectionStatus_ = ConnectionStatus::DISCONNECTED;
                ELEGOO_LOG_ERROR("Protocol connection failed for printer {}: {}",
                                 StringUtils::maskString(printerInfo_.printerId),
                                 connectionResult.message);
                return BizResult<nlohmann::json>{connectionResult.code, connectionResult.message};
            }

            // Connection successful
            isConnected_ = true;
            connectionStatus_ = ConnectionStatus::CONNECTED;

            // Call subclass hook for post-connection initialization
            onConnected(params);

            // Set auth credentials for file uploader
            if (fileUploader_)
            {
                std::map<std::string, std::string> credentials;
                if (!params.username.empty())
                {
                    credentials["username"] = params.username;
                }
                credentials["authMode"] = params.authMode;
                if (!params.password.empty())
                {
                    credentials["password"] = params.password;
                }
                if (!params.token.empty())
                {
                    credentials["token"] = params.token;
                }
                if (!params.accessCode.empty())
                {
                    credentials["accessCode"] = params.accessCode;
                }
                if (!params.pinCode.empty())
                {
                    credentials["pinCode"] = params.pinCode;
                }

                if (!credentials.empty())
                {
                    fileUploader_->setAuthCredentials(credentials);
                    ELEGOO_LOG_DEBUG("Set auth credentials for file uploader for printer {}",
                                     StringUtils::maskString(printerInfo_.printerId));
                }
            }

            ELEGOO_LOG_INFO("Printer {} connected successfully via {} protocol",
                            StringUtils::maskString(printerInfo_.printerId),
                            protocolType_);
            return BizResult<nlohmann::json>::Success();
        }
        catch (const std::exception &e)
        {
            connectionStatus_ = ConnectionStatus::DISCONNECTED;
            std::string detailedError = "Connection exception: " + std::string(e.what()) +
                                        " (Printer: " + StringUtils::maskString(printerInfo_.printerId) +
                                        ", Protocol: " + protocolType_ + ")";

            ELEGOO_LOG_ERROR("Exception connecting printer {}: {}",
                             StringUtils::maskString(printerInfo_.printerId), detailedError);
            return BizResult<nlohmann::json>{ELINK_ERROR_CODE::UNKNOWN_ERROR, detailedError};
        }
    }

    BizResult<nlohmann::json> BasePrinter::disconnect()
    {
        std::lock_guard<std::mutex> lock(statusMutex_);

        ELEGOO_LOG_INFO("Attempting to disconnect printer {} via {} protocol",
                        StringUtils::maskString(printerInfo_.printerId),
                        protocolType_);

        // Call subclass hook before disconnection
        onDisconnecting();

        if (protocol_)
        {
            protocol_->disconnect();
            ELEGOO_LOG_INFO("Protocol {} disconnected for printer {}",
                            protocolType_,
                            StringUtils::maskString(printerInfo_.printerId));
        }
        else
        {
            ELEGOO_LOG_WARN("Protocol not available during disconnect for printer {}",
                            StringUtils::maskString(printerInfo_.printerId));
        }

        // Update connection status
        isConnected_ = false;
        connectionStatus_ = ConnectionStatus::DISCONNECTED;

        ELEGOO_LOG_INFO("Printer {} disconnected successfully",
                        StringUtils::maskString(printerInfo_.printerId));
        return BizResult<nlohmann::json>::Success();
    }

    bool BasePrinter::isConnected() const
    {
        return isConnected_;
    }

    ConnectionStatus BasePrinter::getConnectionStatus() const
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        return connectionStatus_;
    }

    // ========== Printer Control ==========

    BizResult<nlohmann::json> BasePrinter::request(
        const BizRequest &request,
        std::chrono::milliseconds timeout)
    {
        ELEGOO_LOG_DEBUG("[{}] Request details: {}", printerInfo_.host, request.params.dump());

        if (!isConnected())
        {
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR,
                "Printer not connected or protocol not available"};
        }

        // Pre-check printer status
        if (!adapter_ || !protocol_)
        {
            ELEGOO_LOG_ERROR("[{}] Printer not ready for request: {}",
                             printerInfo_.host, StringUtils::maskString(printerInfo_.printerId));
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::UNKNOWN_ERROR, "protocol not available"};
        }

        // Use default timeout if not specified
        if (timeout.count() == 0)
        {
            timeout = getDefaultTimeout();
        }

        // Validate request using subclass logic
        if (!validateRequest(request))
        {
            return BizResult<nlohmann::json>{
                ELINK_ERROR_CODE::INVALID_PARAMETER, "Invalid request"};
        }

        return handleRequest(request, timeout);
    }

    // ========== Callback Settings ==========

    void BasePrinter::setEventCallback(std::function<void(const BizEvent &)> callback)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        eventCallback_ = callback;
    }

    // ========== Virtual Methods (Default Implementations) ==========

    void BasePrinter::onConnected(const ConnectPrinterParams &params)
    {
        // Default implementation: do nothing
        // Subclasses can override for printer-specific initialization
    }

    void BasePrinter::onDisconnecting()
    {
        // Default implementation: do nothing
        // Subclasses can override for printer-specific cleanup
    }

    bool BasePrinter::validateRequest(const BizRequest &request)
    {
        // Default implementation: accept all requests
        // Subclasses can override for printer-specific validation
        return true;
    }

    std::chrono::milliseconds BasePrinter::getDefaultTimeout() const
    {
        // Default timeout: 15 seconds
        return std::chrono::milliseconds(15000);
    }

    std::unique_ptr<IProtocol> BasePrinter::createProtocol()
    {
        // Base class doesn't create protocol - subclasses must override
        ELEGOO_LOG_ERROR("createProtocol() not implemented for printer type: {}",
                         static_cast<int>(printerInfo_.printerType));
        return nullptr;
    }

    std::unique_ptr<IMessageAdapter> BasePrinter::createMessageAdapter()
    {
        // Base class doesn't create adapter - subclasses must override
        ELEGOO_LOG_ERROR("createMessageAdapter() not implemented for printer type: {}",
                         static_cast<int>(printerInfo_.printerType));
        return nullptr;
    }

    std::unique_ptr<IHttpFileTransfer> BasePrinter::createFileUploader()
    {
        // Base class returns nullptr - subclasses can override if they support file upload
        return nullptr;
    }

    // ========== Protected Helper Methods ==========

    void BasePrinter::onMessage(const std::string &messageData)
    {
        try
        {
            if (!adapter_)
            {
                ELEGOO_LOG_ERROR("No adapter available for printer {}",
                                 StringUtils::maskString(printerInfo_.printerId));
                return;
            }

            // Parse message type
            std::vector<std::string> parsedMessageTypes = adapter_->parseMessageType(messageData);
            if (parsedMessageTypes.empty())
            {
                ELEGOO_LOG_ERROR("Failed to parse message type for printer {}: {}",
                                 StringUtils::maskString(printerInfo_.printerId), messageData);
                return;
            }

            for (size_t i = 0; i < parsedMessageTypes.size(); i++)
            {
                const std::string &parsedMessageType = parsedMessageTypes[i];
                if (parsedMessageType == "response")
                {
                    // Convert printer response to standard response format
                    PrinterBizResponse standardResponse = adapter_->convertToResponse(messageData);
                    if (!standardResponse.isValid())
                    {
                        if (standardResponse.code == ELINK_ERROR_CODE::SUCCESS)
                        {
                            continue;
                        }
                        if (standardResponse.message.find("No request mapping found") != std::string::npos)
                        {
                            ELEGOO_LOG_DEBUG("No request mapping found for printer {}",
                                             StringUtils::maskString(printerInfo_.printerId));
                            continue;
                        }

                        std::string maskedContent = messageData;
                        if (!printerInfo_.serialNumber.empty() &&
                            maskedContent.find(printerInfo_.serialNumber) != std::string::npos)
                        {
                            std::string maskSn = StringUtils::maskString(printerInfo_.serialNumber);
                            maskedContent = StringUtils::replaceAll(maskedContent,
                                                                    printerInfo_.serialNumber, maskSn);
                        }

                        std::string mainboardId = adapter_->getPrinterInfo().mainboardId;
                        if (!mainboardId.empty() && maskedContent.find(mainboardId) != std::string::npos)
                        {
                            std::string maskId = StringUtils::maskString(mainboardId);
                            maskedContent = StringUtils::replaceAll(maskedContent, mainboardId, maskId);
                        }

                        ELEGOO_LOG_WARN("Invalid response message for printer {}: {}",
                                        StringUtils::maskString(printerInfo_.printerId), maskedContent);
                        continue;
                    }
                    handleResponseMessage(standardResponse.requestId, standardResponse.code,
                                          standardResponse.message, standardResponse.data);
                }
                else if (parsedMessageType == "event")
                {
                    // Convert printer event to standard event format
                    PrinterBizEvent data = adapter_->convertToEvent(messageData);
                    if (data.isValid())
                    {
                        BizEvent bizEvent;
                        bizEvent.method = data.method;
                        bizEvent.data = data.data.value();
                        ELEGOO_LOG_DEBUG("Received event from printer {}: {}",
                                         StringUtils::maskString(printerInfo_.printerId),
                                         bizEvent.data.dump());
                        handleEventMessage(bizEvent);
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error processing message for printer {}: {}",
                             StringUtils::maskString(printerInfo_.printerId), e.what());
        }
    }

    void BasePrinter::handleResponseMessage(
        const std::string &requestId,
        ELINK_ERROR_CODE code,
        std::string message,
        const std::optional<nlohmann::json> &result)
    {
        if (!requestId.empty())
        {
            std::lock_guard<std::mutex> lock(requestsMutex_);
            auto it = pendingRequests_.find(requestId);
            if (it != pendingRequests_.end())
            {
                if (it->second.promise)
                {
                    try
                    {
                        BizResult<nlohmann::json> res;
                        res.code = code;
                        res.data = result;
                        res.message = message;
                        it->second.promise->set_value(res);
                    }
                    catch (const std::future_error &)
                    {
                        ELEGOO_LOG_WARN("Promise already set for request ID: {}", requestId);
                    }
                }
                pendingRequests_.erase(it);
            }
            else
            {
                ELEGOO_LOG_WARN("Received response for unknown request ID: {}", requestId);
            }
        }
        else
        {
            ELEGOO_LOG_WARN("Received response without request ID from printer {}",
                            StringUtils::maskString(printerInfo_.printerId));
        }
    }

    void BasePrinter::handleEventMessage(const BizEvent &event)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (eventCallback_)
        {
            eventCallback_(event);
        }
    }

    void BasePrinter::cleanupPendingRequests(const std::string &reason)
    {
        std::lock_guard<std::mutex> lock(requestsMutex_);

        if (pendingRequests_.empty())
        {
            return;
        }

        int cleanedCount = static_cast<int>(pendingRequests_.size());
        ELEGOO_LOG_INFO("Cleaning up {} pending requests for printer {}: {}",
                        cleanedCount, StringUtils::maskString(printerInfo_.printerId), reason);

        for (auto &pair : pendingRequests_)
        {
            if (pair.second.promise)
            {
                try
                {
                    pair.second.promise->set_value(
                        BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, reason));
                }
                catch (const std::future_error &)
                {
                    // Promise already set, ignore
                }
            }
        }
        pendingRequests_.clear();
    }

    void BasePrinter::onProtocolStatusChanged(bool connected)
    {
        if (connected == isConnected_)
        {
            ELEGOO_LOG_DEBUG("Connection status for printer {} unchanged: {}",
                             StringUtils::maskString(printerInfo_.printerId),
                             connected ? "Connected" : "Disconnected");
            return;
        }

        isConnected_ = connected;
        connectionStatus_ = connected ? ConnectionStatus::CONNECTED : ConnectionStatus::DISCONNECTED;
        ELEGOO_LOG_INFO("Printer {} connection status changed: {}",
                        StringUtils::maskString(printerInfo_.printerId),
                        (connected ? "Connected" : "Disconnected"));

        BizEvent statusEvent;
        statusEvent.method = MethodType::ON_CONNECTION_STATUS;
        statusEvent.data = ConnectionStatusData{
            printerInfo_.printerId,
            connected ? ConnectionStatus::CONNECTED : ConnectionStatus::DISCONNECTED};
        ELEGOO_LOG_DEBUG("Connection status for printer {}: {}",
                         StringUtils::maskString(printerInfo_.printerId), statusEvent.data.dump());

        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (eventCallback_)
        {
            eventCallback_(statusEvent);
        }

        if (!connected)
        {
            adapter_->clearStatusCache();
            statusEvent.method = MethodType::ON_PRINTER_STATUS;
            PrinterStatusData printerStatusEvent(printerInfo_.printerId);
            printerStatusEvent.printerStatus.state = PrinterState::OFFLINE;
            statusEvent.data = printerStatusEvent;
            ELEGOO_LOG_DEBUG("Printer status for printer {}: {}",
                             StringUtils::maskString(printerInfo_.printerId), statusEvent.data.dump());
            if (eventCallback_)
            {
                eventCallback_(statusEvent);
            }
        }
        else
        {
            // Start status polling thread when connected
            ELEGOO_LOG_DEBUG("Starting status polling for printer {}",
                             StringUtils::maskString(printerInfo_.printerId));
            startStatusPolling();
        }
    }

    void BasePrinter::sendPrinterRequest(const PrinterBizRequest<std::string> &request)
    {
        if (!protocol_ || !isConnected_)
        {
            ELEGOO_LOG_WARN("Cannot send request to printer: printer {} not ready",
                            StringUtils::maskString(printerInfo_.printerId));
            return;
        }

        auto printerId = printerInfo_.printerId;
        try
        {
            std::weak_ptr<elink::IProtocol> weakProtocol = protocol_;
            // Send asynchronously
            std::thread([weakProtocol, request, printerId]()
                        {
                if (auto protocol = weakProtocol.lock())
                {
                    bool result = protocol->sendCommand(request.data);
                    if (!result)
                    {
                        ELEGOO_LOG_ERROR("Failed to send command (method: {}) to printer: {}", 
                                       static_cast<int>(request.method), 
                                       StringUtils::maskString(printerId));
                    }
                    else
                    {
                        ELEGOO_LOG_DEBUG("Successfully sent command (method: {}) to printer: {}", 
                                       static_cast<int>(request.method), 
                                       StringUtils::maskString(printerId));
                    }
                } })
                .detach();
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error sending request (method: {}) to printer {}: {}",
                             static_cast<int>(request.method),
                             StringUtils::maskString(printerId), e.what());
        }
    }

    BizResult<nlohmann::json> BasePrinter::handleRequest(
        const BizRequest &request,
        std::chrono::milliseconds timeout)
    {
        // Use adapter to convert standard request to printer-specific format
        PrinterBizRequest printerBizRequest = adapter_->convertRequest(
            request.method, request.params, timeout);
        if (!printerBizRequest.isValid())
        {
            return BizResult<nlohmann::json>::Error(
                printerBizRequest.code, printerBizRequest.message);
        }

        // Register request waiting for response
        auto promise = registerPendingRequest(printerBizRequest.requestId);
        if (!promise)
        {
            return BizResult<nlohmann::json>::Error(
                ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to register pending request");
        }

        auto future = promise->get_future();

        // Send command
        bool result = protocol_->sendCommand(printerBizRequest.data);
        if (!result)
        {
            {
                std::lock_guard<std::mutex> lock(requestsMutex_);
                pendingRequests_.erase(printerBizRequest.requestId);
            }

            ELEGOO_LOG_ERROR("Failed to send command for printer {}",
                             StringUtils::maskString(printerInfo_.printerId));

            try
            {
                promise->set_value(BizResult<nlohmann::json>::Error(
                    ELINK_ERROR_CODE::PRINTER_COMMAND_FAILED, "Failed to send command"));
            }
            catch (const std::future_error &)
            {
                // Promise already set, ignore
            }

            return future.get();
        }

        ELEGOO_LOG_DEBUG("Command sent for printer {}, waiting for response (timeout: {}ms)",
                         StringUtils::maskString(printerInfo_.printerId), timeout.count());

        // Wait for response with timeout
        if (timeout.count() > 0)
        {
            if (future.wait_for(timeout) == std::future_status::timeout)
            {
                {
                    std::lock_guard<std::mutex> lock(requestsMutex_);
                    auto it = pendingRequests_.find(printerBizRequest.requestId);
                    if (it != pendingRequests_.end())
                    {
                        try
                        {
                            it->second.promise->set_value(BizResult<nlohmann::json>::Error(
                                ELINK_ERROR_CODE::OPERATION_TIMEOUT,
                                "Request timed out after " + std::to_string(timeout.count()) +
                                    " milliseconds"));
                        }
                        catch (const std::future_error &)
                        {
                            // Promise already set
                        }
                        pendingRequests_.erase(it);
                    }
                }
                ELEGOO_LOG_WARN("Request {} for printer {} timed out after {}ms",
                                printerBizRequest.requestId,
                                StringUtils::maskString(printerInfo_.printerId),
                                timeout.count());
            }
        }

        return future.get();
    }

    std::shared_ptr<std::promise<BizResult<nlohmann::json>>> BasePrinter::registerPendingRequest(
        const std::string &requestId)
    {
        auto promise = std::make_shared<std::promise<BizResult<nlohmann::json>>>();

        std::lock_guard<std::mutex> lock(requestsMutex_);
        pendingRequests_[requestId] = {
            requestId,
            promise,
            std::chrono::steady_clock::now()};

        return promise;
    }

    // ========== Print Control Methods Implementation ==========

    VoidResult BasePrinter::startPrint(const StartPrintParams &params)
    {
        return executeRequest<std::monostate>(
            MethodType::START_PRINT,
            params,
            "Starting print",
            std::chrono::milliseconds(10000));
    }

    VoidResult BasePrinter::pausePrint(const PrinterBaseParams &params)
    {
        return executeRequest<std::monostate>(
            MethodType::PAUSE_PRINT,
            params,
            "Pausing print",
            getDefaultTimeout());
    }

    VoidResult BasePrinter::resumePrint(const PrinterBaseParams &params)
    {
        return executeRequest<std::monostate>(
            MethodType::RESUME_PRINT,
            params,
            "Resuming print",
            getDefaultTimeout());
    }

    VoidResult BasePrinter::stopPrint(const PrinterBaseParams &params)
    {
        return executeRequest<std::monostate>(
            MethodType::STOP_PRINT,
            params,
            "Stopping print",
            getDefaultTimeout());
    }

    VoidResult BasePrinter::setAutoRefill(const SetAutoRefillParams &params)
    {
        return executeRequest<std::monostate>(
            MethodType::SET_AUTO_REFILL,
            params,
            "Setting auto refill",
            std::chrono::milliseconds(3000));
    }

    PrinterAttributesResult BasePrinter::getPrinterAttributes(const PrinterAttributesParams &params, int timeout)
    {
        return executeRequest<PrinterAttributesData>(
            MethodType::GET_PRINTER_ATTRIBUTES,
            params,
            "Getting printer attributes",
            std::chrono::milliseconds(timeout));
    }

    PrinterStatusResult BasePrinter::getPrinterStatus(const PrinterStatusParams &params, int timeout)
    {
        return executeRequest<PrinterStatusData>(
            MethodType::GET_PRINTER_STATUS,
            params,
            "Getting printer status",
            std::chrono::milliseconds(timeout));
    }

    GetCanvasStatusResult BasePrinter::getCanvasStatus(const GetCanvasStatusParams &params)
    {
        return executeRequest<CanvasStatus>(
            MethodType::GET_CANVAS_STATUS,
            params,
            "Getting canvas status",
            std::chrono::milliseconds(3000));
    }

    VoidResult BasePrinter::updatePrinterName(const UpdatePrinterNameParams &params)
    {
        return executeRequest<std::monostate>(
            MethodType::UPDATE_PRINTER_NAME,
            params,
            "Updating printer name",
            std::chrono::milliseconds(3000));
    }

    // ========== Status Polling Thread Methods ==========

    void BasePrinter::startStatusPolling()
    {
        {
            std::lock_guard<std::mutex> lock(statusPollingMutex_);

            // Stop existing polling thread if running
            if (statusPollingRunning_)
            {
                ELEGOO_LOG_DEBUG("Status polling already running for printer {}",
                                 StringUtils::maskString(printerInfo_.printerId));
                return;
            }
        }
        
        if (statusPollingThread_.joinable())
        {
            statusPollingThread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(statusPollingMutex_);
            // Start new polling thread
            statusPollingRunning_ = true;
        }

        statusPollingThread_ = std::thread(&BasePrinter::statusPollingThreadFunc, this);

        ELEGOO_LOG_INFO("Status polling thread started for printer {}",
                        StringUtils::maskString(printerInfo_.printerId));
    }

    void BasePrinter::stopStatusPolling()
    {
        {
            std::lock_guard<std::mutex> lock(statusPollingMutex_);
            if (statusPollingRunning_)
            {
                statusPollingRunning_ = false;
            }
        }
        statusPollingCV_.notify_all();
        // Wait for thread to finish
        if (statusPollingThread_.joinable())
        {
            statusPollingThread_.join();
            ELEGOO_LOG_INFO("Status polling thread stopped for printer {}",
                            StringUtils::maskString(printerInfo_.printerId));
        }
    }

    void BasePrinter::statusPollingThreadFunc()
    {
        ELEGOO_LOG_DEBUG("Status polling thread running for printer {}",
                         StringUtils::maskString(printerInfo_.printerId));

        const int retryIntervalMs = 2000; // Poll every 2 seconds
        const int maxRetries = 99999;        // Maximum 30 attempts (60 seconds total)
        int retryCount = 0;

        while (statusPollingRunning_ && retryCount < maxRetries)
        {
            // Check if still connected
            if (!isConnected_)
            {
                ELEGOO_LOG_DEBUG("Printer {} disconnected, stopping status polling",
                                 StringUtils::maskString(printerInfo_.printerId));
                break;
            }

            // Send status request
            if (adapter_)
            {
                ELEGOO_LOG_DEBUG("[Retry {}] Polling status for printer {}",
                                 retryCount + 1,
                                 StringUtils::maskString(printerInfo_.printerId));

                try
                {
                    PrinterStatusParams params;
                    params.printerId = printerInfo_.printerId;

                    // Send status request with timeout
                    auto result = getPrinterStatus(params, 3000);

                    if (result.isSuccess())
                    {
                        ELEGOO_LOG_INFO("Successfully obtained printer status for {}, stopping polling",
                                        StringUtils::maskString(printerInfo_.printerId));
                        // Success, stop polling
                        break;
                    }
                    else
                    {
                        ELEGOO_LOG_WARN("Failed to get printer status for {} (attempt {}/{}): {}",
                                        StringUtils::maskString(printerInfo_.printerId),
                                        retryCount + 1,
                                        maxRetries,
                                        result.message);
                    }
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_ERROR("Exception while polling status for printer {}: {}",
                                     StringUtils::maskString(printerInfo_.printerId),
                                     e.what());
                }
            }

            retryCount++;

            // Wait before next retry (with ability to interrupt)
            if (statusPollingRunning_ && retryCount < maxRetries)
            {
                std::unique_lock<std::mutex> lock(statusPollingMutex_);
                statusPollingCV_.wait_for(lock, std::chrono::milliseconds(retryIntervalMs),
                                          [this]
                                          { return !statusPollingRunning_; });
            }
        }

        if (retryCount >= maxRetries)
        {
            ELEGOO_LOG_WARN("Status polling reached maximum retries ({}) for printer {}",
                            maxRetries,
                            StringUtils::maskString(printerInfo_.printerId));
        }

        std::lock_guard<std::mutex> lock(statusPollingMutex_);
        statusPollingRunning_ = false;
        ELEGOO_LOG_DEBUG("Status polling thread exiting for printer {}",
                         StringUtils::maskString(printerInfo_.printerId));
    }

} // namespace elink
