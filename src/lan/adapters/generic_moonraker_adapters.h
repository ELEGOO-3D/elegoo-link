#pragma once
#include "protocols/protocol_interface.h"
#include <httplib.h>
#include "protocols/websocket_base.h"
#include "protocols/message_adapter.h"
#include "protocols/file_transfer.h"
#include "discovery/printer_discovery.h"
namespace elink
{
    /**
     * generic_moonraker_Printer Adapter
     * Supports message conversion for generic_moonraker_series 3D printers
     */
    class GenericMoonrakerMessageAdapter : public BaseMessageAdapter
    {
    public:
        explicit GenericMoonrakerMessageAdapter(const PrinterInfo &printerInfo);

        // Implement interface methods
        PrinterBizRequest<std::string> convertRequest(MethodType method, const nlohmann::json &request, std::chrono::milliseconds timeout) override;
        PrinterBizResponse<nlohmann::json> convertToResponse(const std::string &printerResponse) override;
        PrinterBizEvent convertToEvent(const std::string &printerMessage) override;
        std::vector<std::string> parseMessageType(const std::string &printerMessage) override;
        std::vector<PrinterType> getSupportedPrinterType() const override { return {PrinterType::GENERIC_FDM_KLIPPER, PrinterType::ELEGOO_FDM_KLIPPER}; }

        std::string getAdapterInfo() const override
        {
            return "GENERIC_MOONRAKER_ADAPTER";
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
        static const std::vector<std::pair<MethodType, std::string>> COMMAND_MAPPING_TABLE;
        std::string mapCommandType(MethodType command);
        MethodType mapPrinterCommand(std::string printerCommand);

        std::optional<PrinterStatusData> handlePrinterStatus(MethodType method, const nlohmann::json &printerJson);
        std::optional<PrinterAttributesData> handlePrinterAttributes(const nlohmann::json &printerJson);

    private:
        // sendMessageToPrinter method inherited from base class

        // Status cache and differential update related methods
        void cacheFullPrinterStatusJson(const nlohmann::json &fullStatusResult);
        nlohmann::json mergeStatusUpdateJson(const nlohmann::json &deltaStatusResult);

        // Status cache related member variables (cache original JSON data)
        mutable std::mutex statusCacheMutex_;
        nlohmann::json cachedFullStatusJson_; // Cached full status original JSON (content of the result field)
        bool hasFullStatusCache_ = false;     // Whether there is a valid full status cache
    };
    /**
     * generic_moonraker_Printer Discovery Strategy
     */
    class GenericMoonrakerDiscoveryStrategy : public IDiscoveryStrategy
    {
    public:
        std::string getDiscoveryMessage() const override;
        int getDefaultPort() const override { return 3000; }
        std::string getBrand() const override { return "Generic"; }
        std::unique_ptr<PrinterInfo> parseResponse(const std::string &response,
                                                   const std::string &senderIp,
                                                   int senderPort) const override;
        std::string getWebUrl(const std::string &host, int port) const override;
        std::string getSupportedAuthMode() const override { return ""; }
    };

    /**
     * Elegoo FDM Printer HTTP Uploader
     * Supports file upload for generic_moonraker_series 3D printers
     */
    class GenericMoonrakerHttpTransfer : public BaseHttpFileTransfer
    {
    public:
        GenericMoonrakerHttpTransfer() = default;
        ~GenericMoonrakerHttpTransfer() = default;

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
        // File upload for both small and large files
        FileUploadResult doFileUpload(
            const PrinterInfo &printerInfo,
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback,
            std::ifstream &file,
            size_t totalSize,
            const std::string &fileName);

        // Large file upload using streaming
        FileUploadResult doLargeFileUpload(
            const PrinterInfo &printerInfo,
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback,
            std::ifstream &file,
            size_t totalSize,
            const std::string &fileName);
    };

    class GenericMoonrakerProtocol : public WebSocketBase
    {
    public:
        GenericMoonrakerProtocol();
        virtual ~GenericMoonrakerProtocol() = default;

        std::string getProtocolType() const override { return "websocket"; }

    protected:
        /**
         * @brief Override URL processing, add oneshot token authentication
         */
        std::string processConnectionUrl(const ConnectPrinterParams &connectParams) override;
    };
} // namespace elink
