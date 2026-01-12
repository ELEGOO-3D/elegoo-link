#pragma once

#include "protocols/protocol_interface.h"
#include <httplib.h>
#include "protocols/websocket_protocol.h"
#include "protocols/message_adapter.h"
#include "protocols/file_transfer.h"
#include "discovery/printer_discovery.h"
namespace elink
{
    /**
     * Elegoo FDM V1 Printer Adapter
     * Supports message conversion for Elegoo FDM V1 series 3D printers
     */
    class ElegooFdmCCMessageAdapter : public BaseMessageAdapter
    {
    public:
        explicit ElegooFdmCCMessageAdapter(const PrinterInfo &printerInfo);

        // Implement interface methods
        PrinterBizRequest<std::string> convertRequest(MethodType method, const nlohmann::json &request, std::chrono::milliseconds timeout) override;
        PrinterBizResponse<nlohmann::json> convertToResponse(const std::string &printerResponse) override;
        PrinterBizEvent convertToEvent(const std::string &printerMessage) override;
        std::vector<std::string> parseMessageType(const std::string &printerMessage) override;
        std::vector<PrinterType> getSupportedPrinterType() const override { return {PrinterType::ELEGOO_FDM_CC}; }

        std::string getAdapterInfo() const override
        {
            return "ELEGOO_FDM_CC_ADAPTER";
        }

        virtual nlohmann::json getCachedFullStatusJson() const override
        {
            return nlohmann::json::object();
        }

    private:
        // Command mapping related data - optimized unified management
        static const std::vector<std::pair<MethodType, int>> COMMAND_MAPPING_TABLE;
        int mapCommandType(MethodType command);
        MethodType mapPrinterCommand(int printerCommand);
        // Create standard data body
        nlohmann::json createStandardBody() const;

        PrinterStatusData handlePrinterStatus(const nlohmann::json &printerJson) const;
        PrinterAttributesData handlePrinterAttributes(const nlohmann::json &printerJson) const;
        std::optional<CanvasStatus> handleCanvasStatus(const nlohmann::json &result) const;
    private:
    };
    /**
     * Elegoo Printer Discovery Strategy
     */
    class ElegooFdmCCDiscoveryStrategy : public IDiscoveryStrategy
    {
    public:
        std::string getDiscoveryMessage() const override;
        int getDefaultPort() const override { return 3000; }
        std::string getBrand() const override { return "Elegoo"; }
        std::unique_ptr<PrinterInfo> parseResponse(const std::string &response,
                                                   const std::string &senderIp,
                                                   int senderPort) const override;
        std::string getWebUrl(const std::string &host, int port) const override;
        std::string getSupportedAuthMode() const override { return ""; }
    };

    /**
     * Elegoo FDM Printer HTTP Uploader
     * Supports file upload for Elegoo FDM series 3D printers
     */
    class ElegooFdmCCHttpTransfer : public BaseHttpFileTransfer
    {
    public:
        ElegooFdmCCHttpTransfer() = default;
        ~ElegooFdmCCHttpTransfer() = default;

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
         * Implements Elegoo-specific file download logic
         */
        FileDownloadResult doDownload(
            const PrinterInfo &printerInfo,
            const FileDownloadParams &params,
            FileDownloadProgressCallback progressCallback) override;

        virtual std::string getDownloadUrl(
            const PrinterInfo &printerInfo,
            const GetDownloadUrlParams &params) override;

    private:
        // Use httplib client to upload a single data chunk (performance optimized version)
        VoidResult uploadChunkWithSession(
            httplib::Client &client,
            const std::vector<char> &data,
            size_t offset,
            size_t totalSize,
            const std::string &fileMD5,
            const std::string &uuid,
            const std::string &fileName);
    };

    class ElegooFdmCCProtocol : public WebSocketBase
    {
    public:
        ElegooFdmCCProtocol();
        virtual ~ElegooFdmCCProtocol() = default;

        std::string getProtocolType() const override { return "websocket"; }

    protected:
        /**
         * @brief Override URL processing, add oneshot token authentication
         */
        std::string processConnectionUrl(const ConnectPrinterParams &connectParams) override;
        bool isHeartbeatEnabled() const override { return true; }
        std::string createHeartbeatMessage() const override { return "ping"; }
    };
} // namespace elink
