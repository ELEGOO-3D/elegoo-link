#include "adapters/generic_moonraker_adapters.h"
#include "utils/logger.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>
#include <fstream>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "utils/utils.h"
#include <filesystem>
namespace elink
{

    std::vector<PrinterType> GenericMoonrakerHttpTransfer::getSupportedPrinterTypes() const
    {
        return {PrinterType::GENERIC_FDM_KLIPPER, PrinterType::ELEGOO_FDM_KLIPPER};
    }

    std::string GenericMoonrakerHttpTransfer::getUploaderInfo() const
    {
        return "generic_moonraker_http_transfer";
    }

    FileUploadResult GenericMoonrakerHttpTransfer::doUpload(
        const PrinterInfo &printerInfo,
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback)
    {
        ELEGOO_LOG_INFO("Starting file upload for file: {} to printer: {}", params.localFilePath, printerInfo.host);

        // Validate input parameters
        if (params.localFilePath.empty())
        {
            ELEGOO_LOG_ERROR("Local file path is empty");
            return FileUploadResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Local file path is empty");
        }

        if (printerInfo.host.empty())
        {
            ELEGOO_LOG_ERROR("Printer host is empty");
            return FileUploadResult::Error(ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR, "Printer host is empty");
        }

        // Check if file exists and is readable - use PathUtils for UTF-8 support
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

        if (totalSize == 0)
        {
            ELEGOO_LOG_ERROR("File is empty: {}", params.localFilePath);
            file.close();
            return FileUploadResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "File is empty");
        }

        ELEGOO_LOG_INFO("File size: {} bytes", totalSize);

        // Get file name from params or extract from path
        std::string fileName = params.fileName.empty() ? std::filesystem::path(params.localFilePath).filename().string() : params.fileName;

        // Define threshold for large file uploads (e.g., 1MB)
        const size_t LARGE_FILE_THRESHOLD = 1 * 1024 * 1024; // 1MB

        if (totalSize > LARGE_FILE_THRESHOLD)
        {
            ELEGOO_LOG_INFO("File size ({} bytes) is large, using streaming upload", totalSize);
            return doLargeFileUpload(printerInfo, params, progressCallback, file, totalSize, fileName);
        }
        else
        {
            ELEGOO_LOG_INFO("File size ({} bytes) is small, using single upload", totalSize);
            return doFileUpload(printerInfo, params, progressCallback, file, totalSize, fileName);
        }
    }

    FileUploadResult GenericMoonrakerHttpTransfer::doFileUpload(
        const PrinterInfo &printerInfo,
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback,
        std::ifstream &file,
        size_t totalSize,
        const std::string &fileName)
    {
        ELEGOO_LOG_INFO("Starting single file upload for: {} ({} bytes)", fileName, totalSize);

        if (printerInfo.host.empty())
        {
            ELEGOO_LOG_ERROR("Invalid host in printer info: {}", printerInfo.host);
            return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Invalid host in printer info");
        }
        std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);

        // Read entire file into memory for single upload
        std::vector<char> fileData(totalSize);
        file.read(fileData.data(), totalSize);

        if (file.fail())
        {
            ELEGOO_LOG_ERROR("Failed to read file data for single upload");
            return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to read file data for single upload");
        }

        // Create httplib client with appropriate settings
        httplib::Client client(endpoint);
        client.set_default_headers({{"User-Agent", ELEGOO_LINK_USER_AGENT},
                                    {"Accept", "application/json"}});
        client.set_connection_timeout(60);
        client.set_read_timeout(180);
        client.set_write_timeout(180);

        // Prepare multipart form data
        httplib::UploadFormDataItems items = {
            {"select", "true", "", ""}, // Auto-select uploaded file
            {"print", "false", "", ""}, // Don't auto-print
            {"file", std::string(fileData.begin(), fileData.end()), fileName, "application/octet-stream"}};

        auto startTime = std::chrono::steady_clock::now();
        size_t uploadedBytes = 0;
        int lastReportedPercentage = -1;

        // Execute POST request with progress tracking
        auto response = client.Post("/api/files/local", items,
                                    [&](size_t offset, size_t length) -> bool
                                    {
                                        // Check for cancellation
                                        if (isUploadCancelled())
                                        {
                                            ELEGOO_LOG_INFO("File upload cancelled for printer: {}", StringUtils::maskString(params.printerId));
                                            return false; // Cancel upload
                                        }

                                        uploadedBytes = offset + length;

                                        if (progressCallback)
                                        {
                                            int currentPercentage = static_cast<int>((uploadedBytes * 100) / totalSize);

                                            // Only report progress if percentage changed to avoid too frequent callbacks
                                            if (currentPercentage != lastReportedPercentage)
                                            {
                                                lastReportedPercentage = currentPercentage;

                                                FileUploadProgressData progress;
                                                progress.printerId = params.printerId;
                                                progress.totalBytes = totalSize;
                                                progress.uploadedBytes = uploadedBytes;
                                                progress.percentage = currentPercentage;

                                                // Report progress and check if upload should continue
                                                if (!progressCallback(progress))
                                                {
                                                    ELEGOO_LOG_INFO("File upload cancelled by callback");
                                                    return false; // Cancel upload
                                                }
                                            }
                                        }

                                        return true; // Continue upload
                                    });

        // Check for connection errors
        if (!response)
        {
            ELEGOO_LOG_ERROR("HTTP request failed for single file upload");
            return FileUploadResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "HTTP request failed for single file upload");
        }

        ELEGOO_LOG_DEBUG("Single upload response code: {}, body: {}", response->status, response->body);

        // Check HTTP status code
        if (response->status < 200 || response->status >= 300)
        {
            ELEGOO_LOG_ERROR("HTTP error response code: {}", response->status);
            return FileUploadResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, StringUtils::formatErrorMessage("Unknown error.", response->status));
        }

        // Ensure final progress is reported as 100% completion
        if (progressCallback && lastReportedPercentage < 100)
        {
            FileUploadProgressData progress;
            progress.printerId = params.printerId;
            progress.totalBytes = totalSize;
            progress.uploadedBytes = totalSize;
            progress.percentage = 100;

            progressCallback(progress);
        }

        // Parse response for any additional error information
        try
        {
            if (!response->body.empty())
            {
                nlohmann::json jsonResponse = nlohmann::json::parse(response->body);

                // Check for API-level errors in response
                if (jsonResponse.contains("error"))
                {
                    std::string error = jsonResponse["error"].get<std::string>();
                    ELEGOO_LOG_ERROR("API error in single upload response: {}", error);
                    return FileUploadResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, "API error: " + error);
                }

                // Log successful upload details
                if (jsonResponse.contains("files") && jsonResponse["files"].contains("local"))
                {
                    auto fileInfo = jsonResponse["files"]["local"];
                    if (fileInfo.contains("name"))
                    {
                        ELEGOO_LOG_INFO("Successfully uploaded file: {}", fileInfo["name"].get<std::string>());
                    }
                }
            }
        }
        catch (const nlohmann::json::parse_error &e)
        {
            // JSON parsing error is not critical for upload success
            ELEGOO_LOG_DEBUG("Could not parse upload response JSON (not critical): {}", e.what());
        }

        ELEGOO_LOG_INFO("Single file upload completed successfully for: {}", fileName);
        return FileUploadResult::Success();
    }

    FileUploadResult GenericMoonrakerHttpTransfer::doLargeFileUpload(
        const PrinterInfo &printerInfo,
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback,
        std::ifstream &file,
        size_t totalSize,
        const std::string &fileName)
    {
        if (printerInfo.host.empty())
        {
            ELEGOO_LOG_ERROR("Invalid host in printer info: {}", printerInfo.host);
            return FileUploadResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Invalid host in printer info");
        }
        std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);

        ELEGOO_LOG_INFO("Starting large file streaming upload for: {} ({} bytes)", fileName, totalSize);
        // Create httplib client with appropriate settings
        httplib::Client client(endpoint);
        client.set_default_headers({{"User-Agent", ELEGOO_LINK_USER_AGENT},
                                    {"Accept", "application/json"}});
        client.set_connection_timeout(60);
        client.set_read_timeout(300); // Increased timeout for large files
        client.set_write_timeout(300);

        // Generate boundary for multipart data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        std::string boundary = "----ElegooLinkBoundary";
        for (int i = 0; i < 16; ++i)
        {
            boundary += "0123456789abcdef"[dis(gen)];
        }

        // Calculate multipart form data size
        std::string form_data_header =
            "--" + boundary + "\r\n"
                              "Content-Disposition: form-data; name=\"select\"\r\n\r\n"
                              "true\r\n"
                              "--" +
            boundary + "\r\n"
                       "Content-Disposition: form-data; name=\"print\"\r\n\r\n"
                       "false\r\n"
                       "--" +
            boundary + "\r\n"
                       "Content-Disposition: form-data; name=\"file\"; filename=\"" +
            fileName + "\"\r\n"
                       "Content-Type: application/octet-stream\r\n\r\n";

        std::string form_data_footer = "\r\n--" + boundary + "--\r\n";

        size_t multipart_form_data_size = form_data_header.size() + totalSize + form_data_footer.size();

        std::string content_type = "multipart/form-data; boundary=" + boundary;

        // Track progress
        size_t uploadedBytes = 0;
        auto startTime = std::chrono::steady_clock::now();
        int percentage = -1;

        // Execute POST request with streaming
        auto response = client.Post(
            "/api/files/local",
            multipart_form_data_size,
            [&](size_t offset, size_t length, httplib::DataSink &sink) -> bool
            {
                // Check for cancellation
                if (isUploadCancelled())
                {
                    ELEGOO_LOG_INFO("File upload cancelled for printer: {}", StringUtils::maskString(params.printerId));
                    return false; // Cancel upload
                }

                try
                {
                    size_t remaining = length;
                    size_t currentOffset = 0;

                    // Send header part
                    if (offset < form_data_header.size())
                    {
                        size_t headerStart = offset;
                        size_t headerLength = (remaining < (form_data_header.size() - headerStart)) ? remaining : (form_data_header.size() - headerStart);
                        if (!sink.write(form_data_header.data() + headerStart, headerLength))
                        {
                            ELEGOO_LOG_ERROR("Failed to write form data header");
                            return false;
                        }
                        remaining -= headerLength;
                        currentOffset += headerLength;
                    }

                    // Send file data part
                    size_t fileDataStart = form_data_header.size();
                    size_t fileDataEnd = fileDataStart + totalSize;

                    if (offset + currentOffset < fileDataEnd && remaining > 0)
                    {
                        size_t fileOffset = (offset + currentOffset > fileDataStart) ? (offset + currentOffset - fileDataStart) : 0;
                        size_t fileLength = (remaining < (totalSize - fileOffset)) ? remaining : (totalSize - fileOffset);

                        // Read file data in chunks
                        const size_t CHUNK_SIZE = 8192; // 8KB chunks
                        std::vector<char> buffer(CHUNK_SIZE);

                        file.seekg(fileOffset);
                        size_t fileRemaining = fileLength;

                        while (fileRemaining > 0)
                        {
                            size_t readSize = (CHUNK_SIZE < fileRemaining) ? CHUNK_SIZE : fileRemaining;
                            file.read(buffer.data(), readSize);

                            size_t actualRead = file.gcount();
                            if (actualRead == 0)
                            {
                                ELEGOO_LOG_ERROR("Failed to read file data at offset {}", fileOffset);
                                return false;
                            }

                            if (!sink.write(buffer.data(), actualRead))
                            {
                                ELEGOO_LOG_ERROR("Failed to write file data chunk");
                                return false;
                            }

                            fileRemaining -= actualRead;
                            uploadedBytes += actualRead;

                            if (progressCallback)
                            {
                                // Calculate percentage
                                int newPercentage = static_cast<int>((uploadedBytes * 100) / totalSize);
                                if (newPercentage != percentage)
                                {
                                    percentage = newPercentage;
                                    FileUploadProgressData progress;
                                    progress.printerId = params.printerId;
                                    progress.totalBytes = totalSize;
                                    progress.uploadedBytes = uploadedBytes;
                                    progress.percentage = percentage;

                                    // Report progress
                                    if (!progressCallback(progress))
                                    {
                                        ELEGOO_LOG_INFO("File download cancelled by callback");
                                        return false; // Stop upload if callback returns false
                                    }
                                }
                            }
                        }

                        remaining -= fileLength;
                        currentOffset += fileLength;
                    }

                    // Send footer part
                    size_t footerStart = fileDataEnd;
                    if (offset + currentOffset >= footerStart && remaining > 0)
                    {
                        size_t footerOffset = offset + currentOffset - footerStart;
                        size_t footerLength = (remaining < (form_data_footer.size() - footerOffset)) ? remaining : (form_data_footer.size() - footerOffset);

                        if (!sink.write(form_data_footer.data() + footerOffset, footerLength))
                        {
                            ELEGOO_LOG_ERROR("Failed to write form data footer");
                            return false;
                        }
                    }

                    return true;
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_ERROR("Exception in streaming upload: {}", e.what());
                    return false;
                }
            },
            content_type.c_str());

        // Check for connection errors
        if (!response)
        {
            ELEGOO_LOG_ERROR("HTTP request failed for large file streaming upload");
            return FileUploadResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "HTTP request failed for large file streaming upload");
        }

        ELEGOO_LOG_DEBUG("Large upload response code: {}, body: {}", response->status, response->body);

        // Check HTTP status code
        if (response->status < 200 || response->status >= 300)
        {
            ELEGOO_LOG_ERROR("HTTP error response code: {}", response->status);
            return FileUploadResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "HTTP error response code: " + std::to_string(response->status));
        }

        // Final progress update
        if (progressCallback)
        {
            FileUploadProgressData progress;
            progress.printerId = params.printerId;
            progress.totalBytes = totalSize;
            progress.uploadedBytes = totalSize;
            progress.percentage = 100;

            progressCallback(progress);
        }

        // Parse response for any additional error information
        try
        {
            if (!response->body.empty())
            {
                nlohmann::json jsonResponse = nlohmann::json::parse(response->body);

                // Check for API-level errors in response
                if (jsonResponse.contains("error"))
                {
                    std::string error = jsonResponse["error"].get<std::string>();
                    ELEGOO_LOG_ERROR("API error in large upload response: {}", error);
                    return FileUploadResult::Error(ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR, "API error: " + error);
                }

                // Log successful upload details
                if (jsonResponse.contains("files") && jsonResponse["files"].contains("local"))
                {
                    auto fileInfo = jsonResponse["files"]["local"];
                    if (fileInfo.contains("name"))
                    {
                        ELEGOO_LOG_INFO("Successfully uploaded large file: {}", fileInfo["name"].get<std::string>());
                    }
                }
            }
        }
        catch (const nlohmann::json::parse_error &e)
        {
            // JSON parsing error is not critical for upload success
            ELEGOO_LOG_DEBUG("Could not parse large upload response JSON (not critical): {}", e.what());
        }

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
        ELEGOO_LOG_INFO("Large file streaming upload completed successfully for: {} in {} seconds", fileName, duration.count());

        return FileUploadResult::Success();
    }

    FileDownloadResult GenericMoonrakerHttpTransfer::doDownload(
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
            std::string path = "/server/files/gcodes" + params.remoteFilePath;

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
    std::string GenericMoonrakerHttpTransfer::getDownloadUrl(
        const PrinterInfo &printerInfo,
        const GetDownloadUrlParams &params)
    {
        std::string endpoint = UrlUtils::extractEndpoint(printerInfo.host);
        // Build download URL for Generic Moonraker printer
        std::string apiUrl = endpoint + "/server/files/gcodes" + params.filePath;
        ELEGOO_LOG_INFO("Getting download URL for file: {}", params.filePath);
        return apiUrl;
    }

}