#pragma once

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>
#include "type.h"
#include "types/internal/internal.h"
#include <mutex>
#include <thread>
namespace elink
{
    template <typename T>
    struct PrinterBizRequest
    {
        std::string requestId;
        MethodType method;
        T data;                                            // data to be sent to the actual printer
        ELINK_ERROR_CODE code = ELINK_ERROR_CODE::SUCCESS; // 0 indicates success, non-0 indicates error code
        std::string message = "ok";                        // Error message
        bool isValid() const
        {
            return code == ELINK_ERROR_CODE::SUCCESS;
        }
    };

    template <typename T>
    struct PrinterBizResponse
    {
        std::string requestId;
        ELINK_ERROR_CODE code = ELINK_ERROR_CODE::SUCCESS; // 0 indicates success, non-0 indicates error code
        std::string message = "ok";                        // Error message
        std::optional<T> data;
        static PrinterBizResponse<T> success()
        {
            return PrinterBizResponse<T>{"", ELINK_ERROR_CODE::SUCCESS, "ok"};
        }
        static PrinterBizResponse<T> error(ELINK_ERROR_CODE errCode, const std::string &msg)
        {
            return PrinterBizResponse<T>{"", errCode, msg};
        }
        // is valid
        bool isValid() const
        {
            return !requestId.empty();
        }
    };

    struct PrinterBizEvent
    {
        MethodType method = MethodType::UNKNOWN; // Command type
        std::optional<nlohmann::json> data;
        PrinterBizEvent(MethodType method = MethodType::UNKNOWN, const nlohmann::json &data = nlohmann::json{})
            : method(method), data(data) {}

        bool isValid() const
        {
            return method != MethodType::UNKNOWN && (data && !data->empty());
        }
    };

    /**
     * Message Adapter Interface - Converts standard messages to printer-specific messages
     * Responsible for converting SDK's standard RequestMessage to a format supported by the printer
     * and converting data returned by the printer to standard ResponseMessage and EventMessage
     */
    class IMessageAdapter
    {
    public:
        virtual ~IMessageAdapter() = default;

        // ========== Request Message Conversion ==========

        /**
         * Convert standard RequestMessage to printer-specific message format (JSON version)
         * @param request JSON format of the standard request message
         * @return Printer-specific message string
         */
        virtual PrinterBizRequest<std::string> convertRequest(MethodType method, const nlohmann::json &request, std::chrono::milliseconds timeout) = 0;

        // ========== Response Message Conversion ==========

        /**
         * Convert data returned by the printer to a standard ResponseMessage
         * @param printerResponse Raw data returned by the printer
         * @param originalRequestId Original request ID (for association)
         * @return JSON format of the standard response message
         */
        virtual PrinterBizResponse<nlohmann::json> convertToResponse(const std::string &printerResponse) = 0;

        /**
         * Convert data returned by the printer to a standard EventMessage
         * @param printerMessage Message actively pushed by the printer
         * @return JSON format of the standard event message
         */
        virtual PrinterBizEvent convertToEvent(const std::string &printerMessage) = 0;

        // ========== Message Parsing ==========
        /**
         * Parse printer message type
         * @param printerMessage Printer message
         * @return Message type ("response", "event", "status", "error")
         */
        virtual std::vector<std::string> parseMessageType(const std::string &printerMessage) = 0;

        // ========== Printer Information ==========

        /**
         * Get supported printer type
         * @return Printer type
         */
        virtual std::vector<PrinterType> getSupportedPrinterType() const = 0;

        /**
         * Adapter information
         */
        virtual std::string getAdapterInfo() const = 0;

        /**
         * Cleanup expired request records
         */
        virtual void cleanupExpiredRequests() = 0;

        /**
         * Set general message send callback function
         * @param callback Callback function, parameter is the printer business request to be sent
         */
        virtual void setMessageSendCallback(std::function<void(const PrinterBizRequest<std::string> &request)> callback) = 0;

        /**
         * Send message to printer
         * @param methodType Method type
         * @param request Request parameters (optional)
         */
        virtual void sendMessageToPrinter(MethodType methodType, const nlohmann::json &request = nlohmann::json::object()) = 0;

        virtual nlohmann::json getCachedFullStatusJson() const = 0;
        virtual PrinterInfo getPrinterInfo() const = 0;
        virtual void clearStatusCache() = 0;
    };

    /**
     * Base Message Adapter - Provides a base implementation with general functionality
     */
    class BaseMessageAdapter : public IMessageAdapter
    {
    public:
        explicit BaseMessageAdapter(const PrinterInfo &printerInfo);
        virtual ~BaseMessageAdapter();

        /**
         * Cleanup expired request records
         * @param maxAgeSeconds Maximum retention time (seconds)
         */
        virtual void cleanupExpiredRequests() override;

        /**
         * Set general message send callback function
         * @param callback Callback function, parameter is the printer business request to be sent
         */
        virtual void setMessageSendCallback(std::function<void(const PrinterBizRequest<std::string> &request)> callback) override;

        /**
         * Send message to printer
         * @param methodType Method type
         * @param request Request parameters
         */
        virtual void sendMessageToPrinter(MethodType methodType, const nlohmann::json &request = nlohmann::json::object()) override;

        void setPrinterInfo(const PrinterInfo &printerInfo)
        {
            printerInfo_ = printerInfo;
        }
        virtual PrinterInfo getPrinterInfo() const override
        {
            return printerInfo_;
        }
        virtual void clearStatusCache() override;
    protected:
        mutable PrinterInfo printerInfo_;

        // Request tracking structure
        struct RequestRecord
        {
            std::string standardMessageId;                   // Standard request messageId
            std::string printerRequestId;                    // Printer-side request ID
            std::chrono::steady_clock::time_point timestamp; // Request timestamp
            MethodType method;                               // Command type
            std::chrono::milliseconds timeout;               // Custom timeout
        };

        // Request tracking map: Printer response ID -> Request record
        mutable std::map<std::string, RequestRecord> pendingRequests_;
        mutable std::mutex requestTrackingMutex_;

        // General message send callback
        std::function<void(const PrinterBizRequest<std::string> &request)> messageSendCallback_;

        // Periodic cleanup for expired requests
        std::thread cleanupThread_;
        std::atomic<bool> shouldStopCleanup_;
        std::condition_variable cleanupCondition_;
        mutable std::mutex cleanupMutex_;

        static constexpr std::chrono::milliseconds CLEANUP_INTERVAL{60000}; // Cleanup interval 60 seconds

        // Cleanup thread methods
        void startCleanupTimer();
        void stopCleanupTimer();
        void cleanupTimerCallback();

        // Helper methods
        std::string generateMessageId() const;
        std::string generatePrinterRequestId() const;
        nlohmann::json parseJson(const std::string &jsonStr) const;
        bool isValidJson(const std::string &str) const;

        // Request tracking methods
        void recordRequest(const std::string &standardMessageId, const std::string &printerRequestId, MethodType command, std::chrono::milliseconds timeout) const;
        RequestRecord findRequestRecord(const std::string &printerResponseId) const;
        void removeRequestRecord(const std::string &printerResponseId) const;

        /**
         * Check if there are cached records for the specified MethodType
         * @param methodType Method type to check
         * @return true if there are cached records, false otherwise
         */
        bool hasMethodTypeRecord(MethodType methodType) const;

        /**
         * Get the oldest cached record for the specified MethodType
         * @param methodType Method type to search for
         * @return Optional RequestRecord, empty if no record found
         */
        std::optional<RequestRecord> getOldestMethodTypeRecord(MethodType methodType) const;
    };

} // namespace elink
