#pragma once

#include <functional>
#include <mutex>
#include <chrono>
#include <httplib.h>
#include "protocols/message_adapter.h"
#include "protocols/file_transfer.h"
#include "discovery/printer_discovery.h"
#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

// Undefine potentially problematic macros
#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif
#include "protocols/mqtt_protocol.h"
namespace elink
{

    /**
     * Elegoo FDM V1 Printer Adapter
     * Supports message conversion for Elegoo FDM V1 series 3D printers
     */
    class ElegooFdmCC2MessageAdapter : public BaseMessageAdapter
    {
    public:
        explicit ElegooFdmCC2MessageAdapter(const PrinterInfo &printerInfo);

        // Implement interface methods
        PrinterBizRequest<std::string> convertRequest(MethodType method, const nlohmann::json &request, std::chrono::milliseconds timeout) override;
        PrinterBizResponse<nlohmann::json> convertToResponse(const std::string &printerResponse) override;
        PrinterBizEvent convertToEvent(const std::string &printerMessage) override;
        std::vector<std::string> parseMessageType(const std::string &printerMessage) override;
        std::vector<PrinterType> getSupportedPrinterType() const override { return {PrinterType::ELEGOO_FDM_CC2}; }

        std::string getAdapterInfo() const override
        {
            return "ELEGOO_FDM_CC2_ADAPTER";
        }

        // Convenient method: Request status refresh
        void requestStatusRefresh()
        {
            sendMessageToPrinter(MethodType::GET_PRINTER_STATUS);
        }

        // Convenient method: Request printer attributes
        void requestPrinterAttributes()
        {
            sendMessageToPrinter(MethodType::GET_PRINTER_ATTRIBUTES);
        }
        void resetStatusSequence();

        nlohmann::json getCachedFullStatusJson() const override
        {
            std::lock_guard<std::mutex> lock(statusCacheMutex_);
            return cachedFullStatusJson_;
        }
        void clearStatusCache() override;
    private:
        // Command mapping related data - optimized unified management
        static const std::vector<std::pair<MethodType, int>> COMMAND_MAPPING_TABLE;
        int mapCommandType(MethodType command);
        MethodType mapPrinterCommand(int printerCommand);
        // Create standard data body
        nlohmann::json createStandardBody() const;
        ELINK_ERROR_CODE convertRequestErrorToElegooError(int code) const;

        std::optional<PrinterStatusData> handlePrinterStatus(MethodType method, const nlohmann::json &printerJson);
        std::optional<PrinterAttributesData> handlePrinterAttributes(const nlohmann::json &printerJson);
        std::optional<CanvasStatus> handleCanvasStatus(const nlohmann::json &result);

        // Status event continuity check related methods
        bool checkStatusEventContinuity(int currentId);
        // sendMessageToPrinter method inherited from base class

        // Status cache and differential update related methods
        void cacheFullPrinterStatusJson(const nlohmann::json &fullStatusResult);
        nlohmann::json mergeStatusUpdateJson(const nlohmann::json &deltaStatusResult);
        

        // Status event continuity monitoring related member variables
        mutable std::mutex statusSequenceMutex_;
        long long lastStatusEventId_ = -1; // ID of the last status event

        // If 5 consecutive non-continuous status events are received, request a full status refresh
        int nonContinuousCount_ = 0;

        int printerStatusSequenceState_ = 0; // Status sequence state, 0: uninitialized, 1: full status data obtained

        // Status cache related member variables (cache original JSON data)
        mutable std::mutex statusCacheMutex_;
        nlohmann::json cachedFullStatusJson_; // Cached full status original JSON (content of the result field)
        bool hasFullStatusCache_ = false;     // Whether there is a valid full status cache
    };
    /**
     * Elegoo Printer Discovery Strategy
     */
    class ElegooFdmCC2DiscoveryStrategy : public IDiscoveryStrategy
    {
    public:
        std::string getDiscoveryMessage() const override;
        int getDefaultPort() const override { return 52700; }
        std::string getBrand() const override { return "Elegoo"; }
        std::unique_ptr<PrinterInfo> parseResponse(const std::string &response,
                                                   const std::string &senderIp,
                                                   int senderPort) const override;
        virtual std::string getWebUrl(const std::string &host, int port) const override;
        std::string getSupportedAuthMode() const override { return "accessCode"; }
    };

    /**
     * Elegoo FDM Printer HTTP Uploader
     * Supports file upload for Elegoo FDM series 3D printers
     */
    class ElegooFdmCC2HttpTransfer : public BaseHttpFileTransfer
    {
    public:
        ElegooFdmCC2HttpTransfer() = default;
        ~ElegooFdmCC2HttpTransfer() = default;

        std::vector<PrinterType> getSupportedPrinterTypes() const override;
        std::string getUploaderInfo() const override;

    protected:
        /**
         * Implements Elegoo-specific chunked upload logic
         * Fully custom implementation for URL construction, header setup, chunk handling, and all details
         */
        FileUploadResult doUpload(
            const PrinterInfo &printerInfo,
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback) override;

        /**
         * Implements Elegoo-specific file download logic (CCS version)
         */
        FileDownloadResult doDownload(
            const PrinterInfo &printerInfo,
            const FileDownloadParams &params,
            FileDownloadProgressCallback progressCallback) override;

        /**
         * Implements Elegoo-specific logic for obtaining download URLs (CCS version)
         */
        std::string getDownloadUrl(
            const PrinterInfo &printerInfo,
            const GetDownloadUrlParams &params) override;

        VoidResult uploadChunkWithSession(
            httplib::Client &client,
            const std::vector<char> &data,
            size_t offset,
            size_t totalSize,
            const std::string &fileMD5,
            const std::string &uuid,
            const std::string &fileName);

    private:
        std::string generateDownloadUrl(const PrinterInfo &printerInfo, const std::string &storageLocation) const;
    };

    /**
     * Elegoo CC2 specific MQTT Protocol Implementation
     * Customizes the base MqttProtocol for Elegoo CC2 printers:
     * - Custom authentication logic
     * - Printer-specific topic management
     * - Registration process for CC2 printers
     * - Custom heartbeat mechanism
     */
    class ElegooCC2MqttProtocol : public MqttProtocol
    {
    public:
        ElegooCC2MqttProtocol();
        virtual ~ElegooCC2MqttProtocol() = default;

    protected:
        virtual std::string processConnectionUrl(const ConnectPrinterParams &connectParams) override;
        /**
         * Generate CC2-specific client ID
         */
        std::string getClientId(const ConnectPrinterParams &connectParams) const override;

        virtual VoidResult validateConnectionParams(const ConnectPrinterParams &connectParams) const override;

        /**
         * Configure CC2-specific authentication
         */
        void configureConnectionOptions(mqtt::connect_options &conn_opts,
                                        const ConnectPrinterParams &connectParams) override;

        /**
         * Get CC2-specific subscription topics
         */
        std::vector<std::string> getSubscriptionTopics(const ConnectPrinterParams &connectParams) const override;

        /**
         * Get CC2-specific command topic
         */
        std::string getCommandTopic(const ConnectPrinterParams &connectParams, const std::string &commandType = "") const override;

        /**
         * CC2 printers require registration
         */
        bool requiresRegistration() const override;

        /**
         * Perform CC2-specific printer registration
         */
        bool performRegistration(const ConnectPrinterParams &connectParam, const std::string &clientId,
                                 std::function<bool(const std::string &, const std::string &)> sendMessageCallback) override;

        virtual bool isRegistrationMessage(const std::string &topic, const std::string &message) const override;

        /**
         * Validate CC2 registration response
         */
        bool validateRegistrationResponse(const std::string &topic,
                                          const std::string &message,
                                          const std::string &clientId,
                                          ELINK_ERROR_CODE &errorCode,
                                          std::string &errorMessage) override;

        /**
         * CC2 registration timeout
         */
        int getRegistrationTimeoutMs() const override;

        /**
         * Handle CC2-specific messages (status updates, events)
         */
        void handleMessage(const std::string &topic, const std::string &payload) override;

        /**
         * CC2 printers support heartbeat
         */
        bool isHeartbeatEnabled() const override;

        /**
         * CC2 heartbeat interval
         */
        int getHeartbeatIntervalSeconds() const override;

        /**
         * Create CC2-specific heartbeat message
         */
        std::string createHeartbeatMessage() const override;

        /**
         * Handle CC2 heartbeat response
         */
        bool handleHeartbeatResponse(const std::string &payload) override;

        /**
         * Get CC2 heartbeat topic
         */
        std::string getHeartbeatTopic(const ConnectPrinterParams &connectParams) const override;

        /**
         * CC2 heartbeat timeout
         */
        int getHeartbeatTimeoutSeconds() const override;

    private:
        mutable std::string clientId_;
        mutable std::string requestId_;
        mutable std::string serialNumber_;

        // Helper methods
        std::string generateRequestId() const;
    };

} // namespace elink
