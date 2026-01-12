#pragma once

#include <string>
#include <memory>
#include <map>
#include <optional>
#include <functional>
#include "types/biz.h"
#include <nlohmann/json.hpp>

namespace elink
{
    /**
     * HTTP response result
     */
    struct HttpResponse
    {
        int statusCode = 0;                         // HTTP status code
        std::string body;                           // Response body
        std::map<std::string, std::string> headers; // Response headers
        std::string contentType;                    // Content type

        // Check if the request was successful
        bool isSuccess() const { return statusCode >= 200 && statusCode < 300; }

        // Parse JSON response
        nlohmann::json toJson() const;
    };

    /**
     * HTTP request configuration
     */
    struct HttpConfig
    {
        int connectTimeoutMs = 30000;                      // Connection timeout in milliseconds
        int readTimeoutMs = 60000;                         // Read timeout in milliseconds
        int writeTimeoutMs = 30000;                        // Write timeout in milliseconds
        bool enableSSLVerification = true;                // Whether to enable SSL verification
        std::string caCertPath;                           // CA certificate path (used when system CA is not available)
        std::string userAgent = "ElegooClient/1.0";        // User agent
        std::map<std::string, std::string> defaultHeaders; // Default request headers
    };

    /**
     * HTTP request timeout options (optional per-request override)
     */
    struct RequestTimeoutConfig
    {
        std::optional<int> connectTimeoutMs;  // Connection timeout override
        std::optional<int> readTimeoutMs;     // Read timeout override

        RequestTimeoutConfig() = default;
        RequestTimeoutConfig(int timeout) : readTimeoutMs(timeout) {}
        RequestTimeoutConfig(int connect, int read) 
            : connectTimeoutMs(connect), readTimeoutMs(read) {}
    };

    /**
     * HTTP client class
     * Encapsulates httplib, supports GET, POST, PUT requests, supports Bearer token authentication
     */
    class HttpClient
    {
    public:
        explicit HttpClient(const std::string &baseUrl);
        explicit HttpClient(const std::string &baseUrl, const HttpConfig &config);
        ~HttpClient();

        // Disallow copy and assignment
        HttpClient(const HttpClient &) = delete;
        HttpClient &operator=(const HttpClient &) = delete;

        // Allow move
        HttpClient(HttpClient &&) noexcept;
        HttpClient &operator=(HttpClient &&) noexcept;

        // ==================== Configuration Management ====================
        /**
         * Set Bearer token
         */
        void setBearerToken(const std::string &token);

        /**
         * Clear Bearer token
         */
        void clearBearerToken();

        /**
         * Set token
         */
        void setToken(const std::string &token);

        /**
         * Clear token
         */
        void clearToken();

        /**
         * Set default request header
         */
        void setDefaultHeader(const std::string &name, const std::string &value);

        /**
         * Remove default request header
         */
        void removeDefaultHeader(const std::string &name);

        /**
         * Set HTTP configuration
         */
        void setConfig(const HttpConfig &config);

        // ==================== HTTP Request Methods ====================
        /**
         * GET request
         * @param path Request path
         * @param headers Additional request headers
         * @param timeout Optional timeout override for this request
         * @return HTTP response result
         */
        BizResult<HttpResponse> get(
            const std::string &path,
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);

        /**
         * POST request - JSON data
         * @param path Request path
         * @param jsonData JSON data
         * @param headers Additional request headers
         * @param timeout Optional timeout override for this request
         * @return HTTP response result
         */
        BizResult<HttpResponse> post(
            const std::string &path,
            const nlohmann::json &jsonData,
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);

        /**
         * POST request - String data
         * @param path Request path
         * @param data Request data
         * @param contentType Content type
         * @param headers Additional request headers
         * @param timeout Optional timeout override for this request
         * @return HTTP response result
         */
        BizResult<HttpResponse> post(
            const std::string &path,
            const std::string &data,
            const std::string &contentType = "text/plain",
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);

        /**
         * POST request - Form data
         * @param path Request path
         * @param formData Form data
         * @param headers extra request headers
         * @param timeout Optional timeout override for this request
         * @return HTTP response result
         */
        BizResult<HttpResponse> postForm(
            const std::string &path,
            const std::map<std::string, std::string> &formData,
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);

        /**
         * PUT request - JSON data
         * @param path Request path
         * @param jsonData JSON data
         * @param headers extra request headers
         * @param timeout Optional timeout override for this request
         * @return HTTP response result
         */
        BizResult<HttpResponse> put(
            const std::string &path,
            const nlohmann::json &jsonData,
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);

        /**
         * PUT request - String data
         * @param path Request path
         * @param data Request data
         * @param contentType  Content type
         * @param headers extra request headers
         * @param timeout Optional timeout override for this request
         * @return HTTP response result
         */
        BizResult<HttpResponse> put(
            const std::string &path,
            const std::string &data,
            const std::string &contentType = "text/plain",
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);

        // ==================== Progress Callback ====================
        /**
         * Upload progress callback function type
         * @param current Number of bytes uploaded
         * @param total Total number of bytes
         * @return Return false to cancel upload
         */
        using ProgressCallback = std::function<bool(uint64_t current, uint64_t total)>;

        /**
         * PUT request - Binary data (safely handles data containing \0)
         * @param path  Request path
         * @param data  Binary data
         * @param headers extra request headers (should include Content-Type)
         * @param timeout Optional timeout override for this request
         * @param progressCallback Progress callback function (optional), return false to cancel upload
         * @return HTTP response result
         */
        BizResult<HttpResponse> put(
            const std::string &path,
            const std::vector<char> &data,
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt,
            const ProgressCallback &progressCallback = nullptr);

        /**
         * DELETE request
         * @param path Request path
         * @param headers extra request headers
         * @param timeout Optional timeout override for this request
         * @return HTTP response result
         */
        BizResult<HttpResponse> del(
            const std::string &path,
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);

        BizResult<HttpResponse> del(
            const std::string &path,
            const std::string &data,
            const std::map<std::string, std::string> &headers = {},
            const std::optional<RequestTimeoutConfig> &timeout = std::nullopt);
        // ==================== Convenience Methods ====================
        /**
         * Send JSON GET request and parse JSON response
         */
        BizResult<nlohmann::json> getJson(
            const std::string &path,
            const std::map<std::string, std::string> &headers = {});

        /**
         * Send JSON POST request and parse JSON response
         */
        BizResult<nlohmann::json> postJson(
            const std::string &path,
            const nlohmann::json &jsonData,
            const std::map<std::string, std::string> &headers = {});

        /**
         * Send JSON PUT request and parse JSON response
         */
        BizResult<nlohmann::json> putJson(
            const std::string &path,
            const nlohmann::json &jsonData,
            const std::map<std::string, std::string> &headers = {});

        // ==================== Status Check ====================
        /**
         * Check if client has configured Bearer token
         */
        bool hasBearerToken() const;

        /**
         * Get base URL
         */
        std::string getBaseUrl() const;

        // ==================== Request Control Methods ====================
        /**
         * Abort the currently executing HTTP request
         * Note: After calling this method, the request will be immediately aborted
         */
        void stop();

        /**
         * Check if client is in running state
         */
        bool isValid() const;

        // ==================== File Upload ====================
        /**
         * Upload file to specified URL
         * @param url Upload address (full URL or relative path)
         * @param filePath Local file path
         * @param headers Additional request headers
         * @param progressCallback Progress callback function (optional)
         * @return HTTP response result
         */
        BizResult<HttpResponse> putFile(
            const std::string &url,
            const std::string &filePath,
            const std::map<std::string, std::string> &headers = {},
            const ProgressCallback &progressCallback = nullptr);

        std::string urlEncode(const std::string &value);
    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace elink
