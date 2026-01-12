#pragma once

#include <string>
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include <optional>
#include "type.h"
#include "types/internal/internal.h"
#include "types/biz.h"

namespace elink 
{

    /**
     * File Transfer Callback Types
     * @return Returns false to cancel the operation, true to continue
     */
    using FileUploadProgressCallback = std::function<bool(const FileUploadProgressData &)>;
    using FileDownloadProgressCallback = std::function<bool(const FileDownloadProgressData &)>;

    /**
     * HTTP File Transfer Interface Base Class
     * Different printer types need to implement different HTTP file transfer logic (including upload, download, obtaining download URLs, etc.)
     */
    class IHttpFileTransfer
    {
    public:
        virtual ~IHttpFileTransfer() = default;

        /**
         * Set authentication credentials
         * Different printer types can set different types of authentication information
         * @param credentials Map of authentication credentials, key is the credential type, value is the credential value
         */
        virtual void setAuthCredentials(const std::map<std::string, std::string>& credentials) = 0;

        /**
         * Perform file upload
         * @param printer Printer instance
         * @param params Upload parameters
         * @param progressCallback Progress callback
         * @return Upload completion result
         */
        virtual FileUploadResult uploadFile(
            const PrinterInfo &printerInfo,
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback = nullptr) = 0;

        /**
         * Cancel file upload
         * @param printerInfo Printer information
         * @param params Cancel parameters
         * @return Operation result
         */
        virtual VoidResult cancelFileUpload() = 0;

        /**
         * Perform file download
         * @param printerInfo Printer information
         * @param params Download parameters
         * @param progressCallback Progress callback
         * @return Download completion result
         */
        virtual FileDownloadResult downloadFile(
            const PrinterInfo &printerInfo,
            const FileDownloadParams &params,
            FileDownloadProgressCallback progressCallback = nullptr) = 0;

        /**
         * Get file download URL
         * @param printerInfo Printer information
         * @param params Parameters for obtaining the URL
         * @return Returns true if successful
         */
        virtual std::string getDownloadUrl(
            const PrinterInfo &printerInfo,
            const GetDownloadUrlParams &params) = 0;

        /**
         * Get supported printer types
         */
        virtual std::vector<PrinterType> getSupportedPrinterTypes() const = 0;

        /**
         * Get uploader name/description
         */
        virtual std::string getUploaderInfo() const = 0;
    };

    /**
     * HTTP File Transfer Base Class Implementation
     * Provides authentication management, specific transfer implementation is determined by subclasses
     */
    class BaseHttpFileTransfer : public IHttpFileTransfer
    {
    public:
        explicit BaseHttpFileTransfer();
        virtual ~BaseHttpFileTransfer();

        // Implement base interface
        FileUploadResult uploadFile(
            const PrinterInfo &printerInfo,
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback = nullptr) override;

        // Implement cancel upload interface
        VoidResult cancelFileUpload() override;

        // Implement authentication credentials interface
        void setAuthCredentials(const std::map<std::string, std::string>& credentials) override;

        // Implement download interface
        FileDownloadResult downloadFile(
            const PrinterInfo &printerInfo,
            const FileDownloadParams &params,
            FileDownloadProgressCallback progressCallback = nullptr) override;

    protected:
        /**
         * Perform the actual file upload - Subclasses need to implement specific upload logic
         * Each printer type fully decides the upload method, data assembly, and other details
         * @param printer Printer instance
         * @param params Upload parameters
         * @param progressCallback Progress callback
         * @return Upload completion result
         */
        virtual FileUploadResult doUpload(
            const PrinterInfo &printerInfo,
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback) = 0;

        /**
         * Perform the actual file download - Subclasses need to implement specific download logic
         * @param printerInfo Printer information
         * @param params Download parameters
         * @param progressCallback Progress callback
         * @return Download completion result
         */
        virtual FileDownloadResult doDownload(
            const PrinterInfo &printerInfo,
            const FileDownloadParams &params,
            FileDownloadProgressCallback progressCallback) = 0;

    protected:
        /**
         * Check if current upload is cancelled
         * @return true if cancelled, false otherwise
         */
        bool isUploadCancelled() const;

    protected:
        // Authentication credentials protected member, accessible by subclasses
        std::map<std::string, std::string> authCredentials_;
        mutable std::mutex credentialsMutex_;

        // Upload cancellation flag for current operation
        bool uploadCancelled_ = false;
        mutable std::mutex uploadCancellationMutex_;
    };



} // namespace elink
