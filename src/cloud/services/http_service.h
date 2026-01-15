#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include "type.h"
#include "../protocols/http_client.h"

namespace elink
{
    /**
     * HTTP service manager
     * Responsible for HTTP API calls and authentication management
     */
    class HttpService
    {
    public:
        HttpService();
        ~HttpService();

        struct PinCodeDetails
        {
            std::string serialNumber;
            std::string model;
            std::string pinCode;
            int64_t expireTime; // Timestamp in seconds, UTC+0
        };
        // Initialization and cleanup
        VoidResult initialize(std::string region = "", std::string userAgent = "", std::string baseUrl = "", std::string caCertPath = "");
        void cleanup();
        bool isInitialized() const;

        // Authentication management
        VoidResult setCredential(const HttpCredential &credential);
        VoidResult clearCredential();
        BizResult<HttpCredential> refreshCredential(const HttpCredential &credential);
        const HttpCredential &getCredential() const;
        VoidResult setRegion(const SetRegionParams &params);
        BizResult<UserInfo> getUserInfo();
        VoidResult logout();
        // Token management
        bool shouldRefreshToken() const;

        void updatePrinters(const std::vector<PrinterInfo> &printers);

        // API calls
        GetPrinterListResult getPrinters();

        BizResult<AgoraCredential> getAgoraCredential();

        BizResult<MqttCredential> getMqttCredential();

        BizResult<PinCodeDetails> checkPincode(const std::string &printerModel, const std::string &pinCode);
        /**
         * Request pre-binding of printer. After this API returns success, the backend will automatically execute the binding process. We need to listen to RTM messages to confirm the binding result.
         * If RTM never receives a successful binding message, you can confirm the binding result by querying the printer list.
         * If it returns failure, it means the pre-binding request submission failed.
         * @param params Pre-binding parameters (serialNumber is required)
         * @return Pre-binding request result (returns the serialNumber on success)
         * @note The serialNumber field in params must not be empty. Use checkPincode() first to obtain serialNumber if needed.
         */
        BizResult<std::string> bindPrinter(const BindPrinterParams &params, bool manualConfirm);

        VoidResult unbindPrinter(const UnbindPrinterParams &params);

        GetFileListResult getFileList(const GetFileListParams &params);

        BizResult<std::string> getThumbnailUrl(const std::string &thumbnailName);

        GetFileDetailResult getFileDetail(const GetFileDetailParams &params, bool needThumbnail = true);

        PrintTaskListResult getPrintTaskList(const PrintTaskListParams &params);

        DeletePrintTasksResult deletePrintTasks(const DeletePrintTasksParams &params);

        BizResult<nlohmann::json> getPrinterStatus(const std::string &printerId);

        BizResult<std::string> uploadFile(const std::string &fileName, const std::string &filePath, std::function<bool(uint64_t current, uint64_t total)> progressCallback = nullptr);

        BizResult<std::string> uploadFileMultipart(const std::string &fileName, const std::string &filePath, std::function<bool(uint64_t current, uint64_t total)> progressCallback = nullptr, size_t partSize = 20 * 1024 * 1024);

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

        std::string buildUrlPath(const std::string &path);

    private:
        // Internal methods
        VoidResult initializeClient(std::string userAgent, std::string baseUrl, std::string caCertPath);
        void cleanupClient();

        VoidResult serverErrorToNetworkError(int serverCode);

        std::string getSerialNumberByPrinterId(const std::string &printerId) const
        {
            // std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto &printer : m_printers)
            {
                if (printer.printerId == printerId)
                {
                    return printer.serialNumber;
                }
            }
            return "";
        }

        VoidResult handleResponse(const HttpResponse &result);

        std::shared_ptr<HttpClient> getHttpClient(bool userClient = false)
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            return m_httpClient;
        }

    private:
        // HTTP client
        std::shared_ptr<HttpClient> m_httpClient;
        // Authentication information
        HttpCredential m_credential;

        // State
        std::atomic<bool> m_initialized{false};

        std::vector<PrinterInfo> m_printers;

        // Token refresh threshold (seconds)
        static constexpr int TOKEN_REFRESH_THRESHOLD_SECONDS = 3600; // Refresh 1 hour in advance

        mutable std::mutex m_clientMutex;
        std::string m_region;
        std::string m_userAgent;
        std::string m_caCertPath;

        /**
         * @brief Base URL
         */
        std::string m_baseUrl;
    };
}
