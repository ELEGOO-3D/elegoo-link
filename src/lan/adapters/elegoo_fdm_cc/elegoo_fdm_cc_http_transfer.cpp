#include "adapters/elegoo_cc_adapters.h"
#include "utils/logger.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "utils/utils.h"
#include "utils/json_utils.h"
namespace elink
{

    // ========== ElegooFdmCCHttpTransfer Implementation ==========

    std::vector<PrinterType> ElegooFdmCCHttpTransfer::getSupportedPrinterTypes() const
    {
        return {PrinterType::ELEGOO_FDM_CC};
    }

    std::string ElegooFdmCCHttpTransfer::getUploaderInfo() const
    {
        return "elegoo_fdm_cc_http_transfer";
    }

    FileUploadResult ElegooFdmCCHttpTransfer::doUpload(
        const PrinterInfo &printerInfo,
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback)
    {
        if (printerInfo.host.empty())
        {
            ELEGOO_LOG_ERROR("Invalid host in printer info: {}", printerInfo.host);
            return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Invalid host in printer info");
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
            return FileUploadResult::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "Failed to open file");
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
            return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to calculate MD5 for file");
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
                return FileUploadResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "File upload cancelled");
            }

            // Calculate current chunk size
            size_t currentChunkSize = (chunkSize < (totalSize - offset)) ? chunkSize : (totalSize - offset);

            // Read data chunk
            std::vector<char> buffer(currentChunkSize);
            file.read(buffer.data(), currentChunkSize);

            if (file.fail() && !file.eof())
            {
                ELEGOO_LOG_ERROR("Failed to read file chunk at offset: {}", offset);
                return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to read file chunk");
            }
            // Get file name from full path
            std::string fileName = params.fileName.empty() ? std::filesystem::path(params.localFilePath).filename().string() : params.fileName;

            // Upload this chunk, using httplib client to reuse connection
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
                progress.percentage = (int)((double)totalTransferred / totalSize * 100.0f);

                bool shouldContinue = progressCallback(progress);
                if (!shouldContinue)
                {
                    ELEGOO_LOG_INFO("Upload cancelled by progress callback");
                    file.close();
                    return FileUploadResult::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED, "Upload cancelled by progress callback");
                }
            }

            // Reduce latency, as using session and connection reuse reduces handshake overhead
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            ELEGOO_LOG_DEBUG("Uploaded chunk {}/{} bytes ({:.1f}%) using session",
                             totalTransferred, totalSize,
                             (double)totalTransferred / totalSize * 100.0f);
        }

        ELEGOO_LOG_INFO("Elegoo chunked upload completed successfully for file: {}", params.localFilePath);
        return FileUploadResult::Success();
    }

    // ========== Elegoo chunked upload implementation ==========

    VoidResult ElegooFdmCCHttpTransfer::uploadChunkWithSession(
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
            // Use httplib to build multipart form
            httplib::UploadFormDataItems items = {
                {"Check", "1", "", ""},
                {"S-File-MD5", fileMD5, "", ""},
                {"Offset", std::to_string(offset), "", ""},
                {"Uuid", uuid, "", ""},
                {"TotalSize", std::to_string(totalSize), "", ""},
                {"File", std::string(data.begin(), data.end()), fileName, "application/octet-stream"}};

            // Execute POST request
            auto response = client.Post("/uploadFile/upload", items);

            // Check for errors
            if (!response)
            {
                ELEGOO_LOG_ERROR("HTTP request failed in chunk upload");
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "HTTP request failed in chunk upload");
            }

            ELEGOO_LOG_DEBUG("Chunk upload response code: {}, body: {}", response->status, response->body);

            // Directly parse Elegoo's response
            if (response->status < 200 || response->status >= 300)
            {
                ELEGOO_LOG_ERROR("HTTP error response code: {}", response->status);
                return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, StringUtils::formatErrorMessage("Unknown error.", response->status));
            }

            try
            {
                // Parse Elegoo's JSON response format
                if (response->body.empty())
                {
                    return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "Empty response body");
                }

                nlohmann::json jsonResponse = nlohmann::json::parse(response->body);

                // Check success field
                if (jsonResponse.contains("code") && jsonResponse["code"].get<std::string>() == "000000")
                {
                    return VoidResult::Success();
                }

                // Check error messages
                if (jsonResponse.contains("messages") && jsonResponse["messages"].is_array())
                {
                    auto messages = jsonResponse["messages"];
                    for (const auto &msg : messages)
                    {
                        if (msg.contains("field") && msg["field"].get<std::string>() == "common_field" && msg.contains("message"))
                        {
                            int errorCode = JsonUtils::safeGetInt(msg, "message", 0);
                            return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, StringUtils::formatErrorMessage("Unknown error.", errorCode));
                            // // Map Elegoo error codes to our error types
                            // switch (errorCode)
                            // {
                            // // case -1: // offset error
                            // // case -2: // offset not match
                            // //     return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_INVALID_PARAMETER, "Offset error: " + std::to_string(errorCode));
                            // // case -3: // file open failed
                            // //     return VoidResult::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND, "File open failed");
                            // // case -4: // unknown error
                            // default:
                            //     // return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, StringUtils::formatErrorMessage("Unknown error.", errorCode));
                            // }
                        }
                    }
                }

                // Default to failure
                return VoidResult::Error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "Unknown response format");
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

    FileDownloadResult ElegooFdmCCHttpTransfer::doDownload(
        const PrinterInfo &printerInfo,
        const FileDownloadParams &params,
        FileDownloadProgressCallback progressCallback)
    {
        try
        {
            if (printerInfo.host.empty())
            {
                ELEGOO_LOG_ERROR("Invalid host in printer info: {}", printerInfo.host);
                return FileDownloadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Invalid host in printer info");
            }

            std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);

            // Build download URL path for Elegoo FDM printer
            std::string path = "/downloadFile" + params.remoteFilePath;

            ELEGOO_LOG_INFO("Starting Elegoo file download from: {}{}", endpoint, path);

            // Create httplib client
            httplib::Client client(endpoint);
            client.set_default_headers({{"User-Agent", ELEGOO_LINK_USER_AGENT},
                                        {"Accept", "*/*"}});
            client.set_connection_timeout(3000); // 3000 seconds timeout

            // Execute HEAD request to get file size
            auto headResponse = client.Head(path.c_str());
            uint64_t totalSize = 0;
            if (headResponse && headResponse->status == 200)
            {
                auto contentLength = headResponse->get_header_value("content-length");
                if (!contentLength.empty())
                {
                    totalSize = std::stoull(contentLength);
                }
            }

            // Create output file - use PathUtils for UTF-8 support
            auto outFile = PathUtils::openOutputStream(params.localFilePath, std::ios::binary);
            if (!outFile.is_open())
            {
                ELEGOO_LOG_ERROR("Failed to create local file: {}", params.localFilePath);
                return FileDownloadResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Failed to create local file: " + params.localFilePath);
            }

            // Set download progress callback
            uint64_t downloadedBytes = 0;
            auto startTime = std::chrono::steady_clock::now();

            // Execute GET request to download file
            auto response = client.Get(path.c_str(), [&](const char *data, size_t data_length)
                                       {
                                           outFile.write(data, data_length);
                                           downloadedBytes += data_length;

                                           if (progressCallback && totalSize > 0)
                                           {
                                               auto currentTime = std::chrono::steady_clock::now();
                                               auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();

                                               FileDownloadProgressData progress;
                                               progress.totalBytes = totalSize;
                                               progress.downloadedBytes = downloadedBytes;
                                               progress.percentage = static_cast<int>((double)downloadedBytes / totalSize * 100.0f);
                                               bool shouldContinue = progressCallback(progress);
                                               if (!shouldContinue)
                                               {
                                                   ELEGOO_LOG_INFO("Download cancelled by progress callback");
                                                   return false; // Cancel download
                                               }
                                           }

                                           return true; // Continue download
                                       });

            outFile.close();

            // Check download result
            if (!response)
            {
                ELEGOO_LOG_ERROR("HTTP request failed in file download");
                std::filesystem::remove(params.localFilePath); // Delete incomplete file
                return FileDownloadResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "HTTP request failed in file download");
            }

            if (response->status < 200 || response->status >= 300)
            {
                ELEGOO_LOG_ERROR("HTTP error response code in download: {}", response->status);
                std::filesystem::remove(params.localFilePath); // Delete incomplete file
                return FileDownloadResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, StringUtils::formatErrorMessage("Unknown error.", response->status));
            }

            ELEGOO_LOG_INFO("File download completed successfully: {}", params.localFilePath);
            return FileDownloadResult::Success();
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Exception in doDownload: {}", e.what());
            return FileDownloadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Exception in doDownload: " + std::string(e.what()));
        }
    }

    std::string ElegooFdmCCHttpTransfer::getDownloadUrl(
        const PrinterInfo &printerInfo,
        const GetDownloadUrlParams &params)
    {
        if (printerInfo.host.empty() || params.filePath.empty())
        {
            ELEGOO_LOG_ERROR("Invalid printer host or file path for download URL");
            return "";
        }

        std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);
        // Build download URL for Elegoo FDM printer
        std::string apiUrl = endpoint + "/downloadFile" + params.filePath;
        ELEGOO_LOG_INFO("Getting download URL for file: {}", params.filePath);
        return apiUrl;
    }

}