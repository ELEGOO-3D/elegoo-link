#include "adapters/elegoo_cc2_adapters.h"
#include "utils/logger.h"
#include <fstream>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
#include <thread>
#include "utils/utils.h"
namespace elink
{
#define CC2_DEFAULT_TOKEN "123456"
    std::vector<PrinterType> ElegooFdmCC2HttpTransfer::getSupportedPrinterTypes() const
    {
        return {PrinterType::ELEGOO_FDM_CC2};
    }

    std::string ElegooFdmCC2HttpTransfer::getUploaderInfo() const
    {
        return "elegoo_fdm_cc2_http_transfer";
    }

    FileUploadResult ElegooFdmCC2HttpTransfer::doUpload(
        const PrinterInfo &printerInfo,
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback)
    {
        if (printerInfo.host.empty())
        {
            ELEGOO_LOG_ERROR("Invalid host in printer info: {}", printerInfo.host);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Invalid host in printer info");
        }
        std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);

        // Prepare HTTP headers
        std::map<std::string, std::string> headers;
        headers["User-Agent"] = ELEGOO_LINK_USER_AGENT;
        headers["Accept"] = "application/json";
        ELEGOO_LOG_INFO("Starting Elegoo chunked upload for file: {}", params.localFilePath);

        // Open file - use PathUtils for UTF-8 support
        auto file = PathUtils::openInputStream(params.localFilePath, std::ios::binary);
        if (!file.is_open())
        {
            ELEGOO_LOG_ERROR("Failed to open file: {}", params.localFilePath);
            return VoidResult::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "Failed to open file");
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t totalSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // Calculate file MD5
        std::string fileMD5 = FileUtils::calculateMD5(params.localFilePath);
        if (fileMD5.empty())
        {
            ELEGOO_LOG_ERROR("Failed to calculate MD5 for file: {}", params.localFilePath);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to calculate MD5 for file");
        }

        // Generate UUID
        std::string uuid = CryptoUtils::generateUUID();

        // Chunk parameters - strictly follow Elegoo API requirements, max 1MB per chunk
        const size_t maxChunkSize = 1024 * 1024; // 1MB per chunk
        size_t chunkSize = maxChunkSize;

        size_t offset = 0;
        size_t totalTransferred = 0;

        ELEGOO_LOG_INFO("File size: {}, MD5: {}, UUID: {}, chunk size: {}",
                        totalSize, fileMD5, uuid, chunkSize);

        // Create httplib client, reuse HTTP connection
        httplib::Client client(endpoint);
        client.set_default_headers({{"User-Agent", ELEGOO_LINK_USER_AGENT},
                                    {"Accept", "application/json"}});
        client.set_connection_timeout(60); // 60 seconds timeout
        client.set_keep_alive(true);       // Enable keep-alive

        while (offset < totalSize)
        {
            // Check for cancellation
            if (isUploadCancelled())
            {
                ELEGOO_LOG_INFO("File upload cancelled for printer: {}", StringUtils::maskString(params.printerId));
                file.close();
                return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "File upload cancelled");
            }

            // Calculate current chunk size
            size_t currentChunkSize = (chunkSize < (totalSize - offset)) ? chunkSize : (totalSize - offset);

            // Read data chunk
            std::vector<char> buffer(currentChunkSize);
            file.read(buffer.data(), currentChunkSize);

            if (file.fail() && !file.eof())
            {
                ELEGOO_LOG_ERROR("Failed to read file chunk at offset: {}", offset);
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to read file chunk");
            }
            // Get file name from full path
            std::string fileName = params.fileName.empty() ? std::filesystem::path(params.localFilePath).filename().string() : params.fileName;

            // Upload this data chunk, reuse connection using httplib client
            auto chunkResult = uploadChunkWithSession(
                client, buffer, offset, totalSize, fileMD5, uuid, fileName);

            if (chunkResult.isError())
            {
                ELEGOO_LOG_ERROR("Failed to upload chunk at offset: {}", offset);
                return chunkResult;
            }

            // Update progress
            offset += currentChunkSize;
            totalTransferred += currentChunkSize;

            if (progressCallback)
            {
                FileUploadProgressData progress;
                progress.printerId = params.printerId;
                progress.totalBytes = totalSize;
                progress.uploadedBytes = totalTransferred;
                progress.percentage = static_cast<int>((double)totalTransferred / totalSize * 100.0);

                bool shouldContinue = progressCallback(progress);
                if (!shouldContinue)
                {
                    ELEGOO_LOG_INFO("Upload cancelled by progress callback");
                    file.close();
                    return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Upload cancelled by progress callback");
                }
            }

            // Reduce latency, as using session and connection reuse reduces handshake overhead
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            ELEGOO_LOG_DEBUG("Uploaded chunk {}/{} bytes ({:.1f}%) using session",
                             totalTransferred, totalSize,
                             (double)totalTransferred / totalSize * 100.0f);
        }

        ELEGOO_LOG_INFO("Elegoo chunked upload completed successfully for file: {}", params.fileName);
        return VoidResult::Success();
    }

    // ========== Elegoo chunked upload implementation ==========

    VoidResult ElegooFdmCC2HttpTransfer::uploadChunkWithSession(
        httplib::Client &client,
        const std::vector<char> &data,
        size_t offset,
        size_t totalSize,
        const std::string &fileMD5,
        const std::string &uuid,
        const std::string &fileName)
    {
        try
        {
            // Construct Content-Range header, format: bytes start-end/total
            size_t endOffset = offset + data.size() - 1;
            std::string contentRange = "bytes " + std::to_string(offset) + "-" +
                                       std::to_string(endOffset) + "/" + std::to_string(totalSize);

            // Set request headers required by CCS
            httplib::Headers headers = {
                {"Content-Type", "application/octet-stream"},
                {"Content-Length", std::to_string(data.size())},
                {"Content-Range", contentRange},
                {"X-File-Name", fileName},
                {"X-File-MD5", fileMD5}};

            // Set Token using dynamic authentication info
            {
                std::lock_guard<std::mutex> lock(credentialsMutex_);
                if (authCredentials_.at("authMode") == "accessCode" && authCredentials_.find("accessCode") != authCredentials_.end())
                {
                    // CCS can use accessCode as token
                    headers.emplace("X-Token", authCredentials_.at("accessCode"));
                    ELEGOO_LOG_DEBUG("Using accessCode as token for CC2 upload");
                }
                else
                {
                    headers.emplace("X-Token", std::string(CC2_DEFAULT_TOKEN));
                    ELEGOO_LOG_DEBUG("No token or accessCode found in auth credentials, using default URL");
                }
            }

            // Execute PUT request, reuse connection
            auto response = client.Put("/upload", headers, std::string(data.begin(), data.end()), "application/octet-stream");

            // Check for errors
            if (!response)
            {
                ELEGOO_LOG_ERROR("HTTP request failed in chunk upload");
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "HTTP request failed in chunk upload");
            }

            ELEGOO_LOG_DEBUG("Chunk upload response code: {}, body: {}", response->status, response->body);
            {
                ELINK_ERROR_CODE errorCode = ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR;
                std::string message;
                // First handle errors based on HTTP status code
                switch (response->status)
                {
                case 200:
                    // Success, continue to parse response body
                    errorCode = ELINK_ERROR_CODE::SUCCESS;
                    break;
                case 401:
                    message = "Unauthorized - token expired or not provided";
                    ELEGOO_LOG_ERROR(message);
                    errorCode = ELINK_ERROR_CODE::INVALID_ACCESS_CODE;
                    break;
                case 403:
                    message = "Forbidden - no permission to access";
                    ELEGOO_LOG_ERROR(message);
                    errorCode = ELINK_ERROR_CODE::PRINTER_ACCESS_DENIED;
                    break;
                case 429:
                    message = "Too Many Requests - rate limit exceeded";
                    ELEGOO_LOG_ERROR(message);
                    errorCode = ELINK_ERROR_CODE::PRINTER_BUSY;
                    break;
                default:
                    if (response->status < 200 || response->status >= 300)
                    {
                        message = "Unknown error.";
                        ELEGOO_LOG_ERROR((message + std::to_string(response->status)));
                        errorCode = ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR;
                    }
                    break;
                }

                if (errorCode != ELINK_ERROR_CODE::SUCCESS)
                {
                    return VoidResult::Error(errorCode, StringUtils::formatErrorMessage(message, response->status));
                }
            }

            try
            {
                // Parse CCS JSON response format
                if (response->body.empty())
                {
                    ELEGOO_LOG_ERROR("Empty response body");
                    return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "Empty response body");
                }

                nlohmann::json jsonResponse = nlohmann::json::parse(response->body);

                // Check error_code field
                if (jsonResponse.contains("error_code"))
                {
                    int printerReturnCode = jsonResponse.value("error_code", -1);
                    ELINK_ERROR_CODE errorCode = ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR;
                    std::string message = "";
                    // Map CCS error codes to our error types
                    switch (printerReturnCode)
                    {
                    case 0:
                        message = "Upload chunk successful";
                        ELEGOO_LOG_DEBUG(message);
                        errorCode = ELINK_ERROR_CODE::SUCCESS;
                        break;
                    case 1000:
                        message = "Token validation failed";
                        ELEGOO_LOG_ERROR(message);
                        errorCode = ELINK_ERROR_CODE::INVALID_ACCESS_CODE;
                        break;
                    default:
                        message = "Unknown error.";
                        ELEGOO_LOG_ERROR((message + std::to_string(printerReturnCode)));
                        errorCode = ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR;
                        break;
                    }
                    if (errorCode != ELINK_ERROR_CODE::SUCCESS)
                    {
                        return VoidResult::Error(errorCode, StringUtils::formatErrorMessage(message, printerReturnCode));
                    }
                    return VoidResult::Success();
                }
                // If no explicit error code, return failure by default
                ELEGOO_LOG_ERROR("No error_code found in response, assuming failure");
                return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "No error_code found in response");
            }
            catch (const nlohmann::json::parse_error &e)
            {
                ELEGOO_LOG_ERROR("Failed to parse Elegoo upload response JSON: {}", e.what());
                return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "Failed to parse JSON response: " + std::string(e.what()));
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Exception in uploadChunkWithSession: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Exception in upload: " + std::string(e.what()));
        }
    }

    FileDownloadResult ElegooFdmCC2HttpTransfer::doDownload(
        const PrinterInfo &printerInfo,
        const FileDownloadParams &params,
        FileDownloadProgressCallback progressCallback)
    {
        try
        {
            // Construct the download URL for Elegoo FDM CCS printer
            std::string baseUrl = generateDownloadUrl(printerInfo, params.storageLocation);
            if (baseUrl.empty())
            {
                ELEGOO_LOG_ERROR("Failed to generate download URL for storage location: {}", params.storageLocation);
                return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Failed to generate download URL for storage location: " + params.storageLocation);
            }

            std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);

            // Extract hostname and path
            std::string path;

            // Extract path part from baseUrl
            size_t pathStart = baseUrl.find("://");
            if (pathStart != std::string::npos)
            {
                pathStart = baseUrl.find("/", pathStart + 3);
                if (pathStart != std::string::npos)
                {
                    path = baseUrl.substr(pathStart);
                }
            }
            else
            {
                // If baseUrl does not contain protocol, assume it is full path
                path = baseUrl;
            }

            path += "?file_name=" + params.remoteFilePath;

            ELEGOO_LOG_INFO("Starting Elegoo CC2 file download from: {}{}", endpoint, path);

            // Create httplib client
            httplib::Client client(endpoint);
            client.set_default_headers({{"User-Agent", ELEGOO_LINK_USER_AGENT},
                                        {"Accept", "*/*"},
                                        {"Connection", "keep-alive"}});
            client.set_connection_timeout(3000); // 3000 seconds timeout

            // Create output file - use PathUtils for UTF-8 support
            auto outFile = PathUtils::openOutputStream(params.localFilePath, std::ios::binary);
            if (!outFile.is_open())
            {
                ELEGOO_LOG_ERROR("Failed to create local file: {}", params.localFilePath);
                return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Failed to create local file: " + params.localFilePath);
            }

            // Set download progress callback
            uint64_t downloadedBytes = 0;
            auto startTime = std::chrono::steady_clock::now();

            // Execute GET request to download file
            auto response = client.Get(path.c_str(), [&](const char *data, size_t data_length)
                                       {
                outFile.write(data, data_length);
                downloadedBytes += data_length;

                if (progressCallback)
                {
                    auto currentTime = std::chrono::steady_clock::now();
                    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();

                    FileDownloadProgressData progress;
                    progress.downloadedBytes = downloadedBytes;
                    bool shouldContinue = progressCallback(progress);
                    if (!shouldContinue)
                    {
                        ELEGOO_LOG_INFO("Download cancelled by progress callback");
                        return false; // Cancel download
                    }
                }

                return true; });

            outFile.close();

            // Check download result
            if (!response)
            {
                ELEGOO_LOG_ERROR("HTTP request failed in file download");
                std::filesystem::remove(params.localFilePath);
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "HTTP request failed in file download");
            }

            if (response->status == 200)
            {
                ELEGOO_LOG_INFO("File download completed successfully: {}", params.localFilePath);
                return VoidResult::Success();
            }
            else
            {
                ELEGOO_LOG_ERROR("HTTP error response code in download: {}", response->status);
                std::filesystem::remove(params.localFilePath);
                return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, StringUtils::formatErrorMessage("Unknown error.", response->status));
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Exception in doDownload: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Exception in doDownload: " + std::string(e.what()));
        }
    }

    std::string ElegooFdmCC2HttpTransfer::getDownloadUrl(
        const PrinterInfo &printerInfo,
        const GetDownloadUrlParams &params)
    {
        std::string url = generateDownloadUrl(printerInfo, params.storageLocation);
        if (url.empty())
        {
            ELEGOO_LOG_ERROR("Failed to generate download URL for storage location: {}", params.storageLocation);
            return "";
        }
        url += "?file_name=" + params.filePath;

        return url;
    }

    std::string ElegooFdmCC2HttpTransfer::generateDownloadUrl(const PrinterInfo &printerInfo, const std::string &storageLocation) const
    {
        if (printerInfo.host.empty())
        {
            ELEGOO_LOG_ERROR("Invalid host in printer info: {}", printerInfo.host);
            return "";
        }
        std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);

        std::string url;
        if (storageLocation == "local")
        {
            url = endpoint + "/download";
        }
        else if (storageLocation == "sdcard")
        {
            url = endpoint + "/download/sdcard";
        }
        else if (storageLocation == "udisk")
        {
            url = endpoint + "/download/udisk";
        }
        else
        {
            url = endpoint + "/download";
        }

        {
            std::lock_guard<std::mutex> lock(credentialsMutex_);
            if (authCredentials_.at("authMode") == "accessCode" && authCredentials_.find("accessCode") != authCredentials_.end())
            {
                // CCS can use accessCode as token
                url += "?X-Token=" + authCredentials_.at("accessCode");
                ELEGOO_LOG_DEBUG("Using accessCode as token for CC2 upload");
            }
            else
            {
                url = url + "?X-Token=" + CC2_DEFAULT_TOKEN;
                ELEGOO_LOG_DEBUG("No token or accessCode found in auth credentials, using default URL");
            }
        }
        return url;
    }

} // namespace elink
