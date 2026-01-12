#include "protocols/file_transfer.h"
#include "core/printer.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <chrono>
#include <thread>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

// MD5 calculation related
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <openssl/md5.h>
#endif

#ifdef min
#undef min
#endif

namespace elink 
{

    // ========== BaseHttpFileTransfer implementation ==========

    BaseHttpFileTransfer::BaseHttpFileTransfer()
    {
        // Initialize
    }

    BaseHttpFileTransfer::~BaseHttpFileTransfer()
    {
        // Cleanup
    }

    void BaseHttpFileTransfer::setAuthCredentials(const std::map<std::string, std::string> &credentials)
    {
        std::lock_guard<std::mutex> lock(credentialsMutex_);
        authCredentials_ = credentials;
        ELEGOO_LOG_DEBUG("Auth credentials updated with {} entries", credentials.size());
    }

    FileUploadResult BaseHttpFileTransfer::uploadFile(
        const PrinterInfo &printerInfo,
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback)
    {
        // Clear cancellation flag at the start of upload
        {
            std::lock_guard<std::mutex> lock(uploadCancellationMutex_);
            uploadCancelled_ = false;
        }

        // Directly call the subclass implementation
        return doUpload(printerInfo, params, progressCallback);
    }

    VoidResult BaseHttpFileTransfer::cancelFileUpload()
    {
        std::lock_guard<std::mutex> lock(uploadCancellationMutex_);
        uploadCancelled_ = true;
        ELEGOO_LOG_INFO("File upload cancellation requested");
        return VoidResult::Success();
    }

    bool BaseHttpFileTransfer::isUploadCancelled() const
    {
        std::lock_guard<std::mutex> lock(uploadCancellationMutex_);
        return uploadCancelled_;
    }

    FileDownloadResult BaseHttpFileTransfer::downloadFile(
        const PrinterInfo &printerInfo,
        const FileDownloadParams &params,
        FileDownloadProgressCallback progressCallback)
    {
        // Directly call the subclass implementation
        return doDownload(printerInfo, params, progressCallback);
    }
} // namespace elink
