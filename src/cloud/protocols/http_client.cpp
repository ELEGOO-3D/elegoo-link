#include "protocols/http_client.h"
#include "utils/logger.h"
#include "utils/utils.h"

#include <curl/curl.h>
#include <regex>
#include <fstream>
#include <atomic>
#include <vector>
#include <sstream>

namespace elink
{
    // HttpResponse method implementation
    nlohmann::json HttpResponse::toJson() const
    {
        if (body.empty())
        {
            return nlohmann::json{};
        }

        try
        {
            return nlohmann::json::parse(body);
        }
        catch (const nlohmann::json::parse_error &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse JSON response: {}", e.what());
            throw;
        }
    }

    // HttpClient implementation
    class HttpClient::Impl
    {
    public:
        std::string baseUrl;
        std::string bearerToken;
        std::string token;
        HttpConfig config;
        std::atomic<bool> shouldStop{false};
        bool useSystemCA{false};
        std::string sslVersion;

        explicit Impl(const std::string &url) : baseUrl(url)
        {
            initializeCurl();
        }

        explicit Impl(const std::string &url, const HttpConfig &cfg)
            : baseUrl(url), config(cfg)
        {
            initializeCurl();
        }

        ~Impl()
        {
            curl_global_cleanup();
        }

        void initializeCurl()
        {
            if(!baseUrl.empty())
            {
                // Ensure URL is HTTPS
                if (baseUrl.find("https://") != 0)
                {
                    ELEGOO_LOG_WARN("Converting HTTP URL to HTTPS: {}", baseUrl);
                    if (baseUrl.find("http://") == 0)
                    {
                        baseUrl = "https://" + baseUrl.substr(7);
                    }
                    else
                    {
                        baseUrl = "https://" + baseUrl;
                    }
                }
            }

            // Initialize curl globally (thread-safe if called multiple times)
            curl_global_init(CURL_GLOBAL_DEFAULT);

            // Check SSL backend once during initialization
            if (config.enableSSLVerification)
            {
                curl_version_info_data *version_info = curl_version_info(CURLVERSION_NOW);
                if (version_info && version_info->ssl_version)
                {
                    sslVersion = version_info->ssl_version;
                    // SChannel (Windows native) and SecureTransport/DarwinSSL (macOS native) use system CA store
                    if (sslVersion.find("Schannel") != std::string::npos ||
                        sslVersion.find("SecureTransport") != std::string::npos ||
                        sslVersion.find("DarwinSSL") != std::string::npos)
                    {
                        useSystemCA = true;
                        ELEGOO_LOG_INFO("Using system CA certificates ({})", sslVersion);
                    }
                    else
                    {
                        ELEGOO_LOG_INFO("SSL backend: {}, will use custom CA if provided", sslVersion);
                    }
                }
            }

            ELEGOO_LOG_DEBUG("HTTPS client initialized for: {}", baseUrl);
        }

        CURL *createCurlHandle()
        {
            CURL *curl = curl_easy_init();
            if (!curl)
            {
                return nullptr;
            }

            // Set SSL verification
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config.enableSSLVerification ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config.enableSSLVerification ? 2L : 0L);

            // Configure CA certificates based on initialization result
            if (config.enableSSLVerification)
            {
                // If not using system CA and custom CA cert path is provided, use it
                if (!useSystemCA && !config.caCertPath.empty() && FileUtils::fileExists(config.caCertPath))
                {
                    curl_easy_setopt(curl, CURLOPT_CAINFO, config.caCertPath.c_str());
                }
                else if (!useSystemCA && config.caCertPath.empty())
                {
                    ELEGOO_LOG_WARN("System CA not supported and no custom CA certificate provided");
                }
            }

            // Set timeouts (in milliseconds, but curl uses seconds for TIMEOUT)
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, config.connectTimeoutMs);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.readTimeoutMs);

            // Set User-Agent
            curl_easy_setopt(curl, CURLOPT_USERAGENT, config.userAgent.c_str());

            // Follow redirects
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            return curl;
        }

        void applyTimeout(CURL *curl, const std::optional<RequestTimeoutConfig> &timeout)
        {
            if (timeout.has_value())
            {
                if (timeout->connectTimeoutMs.has_value())
                {
                    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout->connectTimeoutMs.value());
                }
                if (timeout->readTimeoutMs.has_value())
                {
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout->readTimeoutMs.value());
                }
            }
        }

        struct curl_slist *buildHeaders(const std::map<std::string, std::string> &additionalHeaders = {})
        {
            struct curl_slist *headers = nullptr;

            // Add default headers
            for (const auto &[key, value] : config.defaultHeaders)
            {
                std::string header = key + ": " + value;
                headers = curl_slist_append(headers, header.c_str());
            }

            if (!token.empty())
            {
                std::string header = "token: " + token;
                headers = curl_slist_append(headers, header.c_str());
            }

            // Add Bearer token
            if (!bearerToken.empty())
            {
                std::string header = "Authorization: Bearer " + bearerToken;
                headers = curl_slist_append(headers, header.c_str());
            }

            // Add additional headers
            for (const auto &[key, value] : additionalHeaders)
            {
                std::string header = key + ": " + value;
                headers = curl_slist_append(headers, header.c_str());
            }

            return headers;
        }

        static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
        {
            size_t totalSize = size * nmemb;
            std::string *response = static_cast<std::string *>(userp);
            response->append(static_cast<char *>(contents), totalSize);
            return totalSize;
        }

        static size_t headerCallback(char *buffer, size_t size, size_t nitems, void *userdata)
        {
            size_t totalSize = size * nitems;
            auto *headers = static_cast<std::map<std::string, std::string> *>(userdata);

            std::string header(buffer, totalSize);
            size_t colonPos = header.find(':');
            if (colonPos != std::string::npos)
            {
                std::string key = header.substr(0, colonPos);
                std::string value = header.substr(colonPos + 1);

                // Trim whitespace
                key.erase(0, key.find_first_not_of(" \t\r\n"));
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);

                (*headers)[key] = value;
            }

            return totalSize;
        }

        // URL encode string using curl's built-in function
        // This handles UTF-8 and special characters correctly across all platforms
        std::string urlEncode(const std::string &value)
        {
            CURL *curl = curl_easy_init();
            if (!curl)
            {
                ELEGOO_LOG_ERROR("Failed to initialize curl for URL encoding");
                return value; // Return original value as fallback
            }

            char *encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.length()));
            if (!encoded)
            {
                curl_easy_cleanup(curl);
                ELEGOO_LOG_ERROR("Failed to encode URL");
                return value; // Return original value as fallback
            }

            std::string result(encoded);
            curl_free(encoded);
            curl_easy_cleanup(curl);

            return result;
        }

        BizResult<HttpResponse> performRequest(CURL *curl, const std::string &url, struct curl_slist *headers)
        {
            if (!curl)
            {
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "Failed to initialize curl");
            }

            HttpResponse response;
            std::string responseBody;

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

            if (headers)
            {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }

            CURLcode res = curl_easy_perform(curl);

            if (headers)
            {
                curl_slist_free_all(headers);
            }

            if (res != CURLE_OK)
            {
                std::string errorMsg = curl_easy_strerror(res);
                curl_easy_cleanup(curl);

                ELEGOO_LOG_ERROR("HTTP request failed: {}", errorMsg);

                ELINK_ERROR_CODE errorCode = ELINK_ERROR_CODE::NETWORK_ERROR;
                if (res == CURLE_OPERATION_TIMEDOUT)
                {
                    errorCode = ELINK_ERROR_CODE::OPERATION_TIMEOUT;
                }

                return BizResult<HttpResponse>::Error(errorCode, errorMsg);
            }

            long statusCode;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

            response.statusCode = static_cast<int>(statusCode);
            response.body = responseBody;

            // Set content type
            auto contentTypeIt = response.headers.find("Content-Type");
            if (contentTypeIt != response.headers.end())
            {
                response.contentType = contentTypeIt->second;
            }

            curl_easy_cleanup(curl);

            ELEGOO_LOG_DEBUG("HTTP Response: {} - {}", response.statusCode, response.body.substr(0, 500));

            return BizResult<HttpResponse>::Ok(std::move(response));
        }
    };

    // HttpClient constructor and destructor
    HttpClient::HttpClient(const std::string &baseUrl)
        : m_impl(std::make_unique<Impl>(baseUrl))
    {
    }

    HttpClient::HttpClient(const std::string &baseUrl, const HttpConfig &config)
        : m_impl(std::make_unique<Impl>(baseUrl, config))
    {
    }

    HttpClient::~HttpClient() = default;

    HttpClient::HttpClient(HttpClient &&) noexcept = default;
    HttpClient &HttpClient::operator=(HttpClient &&) noexcept = default;

    // Configuration management methods
    void HttpClient::setBearerToken(const std::string &token)
    {
        m_impl->bearerToken = token;
        ELEGOO_LOG_DEBUG("Bearer token set");
    }

    void HttpClient::clearBearerToken()
    {
        m_impl->bearerToken.clear();
        ELEGOO_LOG_DEBUG("Bearer token cleared");
    }

    void HttpClient::setToken(const std::string &token)
    {
        m_impl->token = token;
        ELEGOO_LOG_DEBUG("Token set");
    }

    void HttpClient::clearToken()
    {
        m_impl->token.clear();
        ELEGOO_LOG_DEBUG("Token cleared");
    }

    void HttpClient::setDefaultHeader(const std::string &name, const std::string &value)
    {
        m_impl->config.defaultHeaders[name] = value;
        ELEGOO_LOG_DEBUG("Default header set: {} = {}", name, value);
    }

    void HttpClient::removeDefaultHeader(const std::string &name)
    {
        m_impl->config.defaultHeaders.erase(name);
        ELEGOO_LOG_DEBUG("Default header removed: {}", name);
    }

    void HttpClient::setConfig(const HttpConfig &config)
    {
        m_impl->config = config;
        // No need to reinitialize with curl
        ELEGOO_LOG_DEBUG("HTTP client config updated");
    }

    // HTTP request method implementation
    BizResult<HttpResponse> HttpClient::get(
        const std::string &path,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            ELEGOO_LOG_DEBUG("GET request: {}", path);

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);

            std::string url = m_impl->baseUrl + path;
            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("GET request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::post(
        const std::string &path,
        const nlohmann::json &jsonData,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            std::string jsonStr = jsonData.dump();
            ELEGOO_LOG_DEBUG("POST JSON request: {} - {}", path, jsonStr.substr(0, 200));

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);
            curlHeaders = curl_slist_append(curlHeaders, "Content-Type: application/json");

            std::string url = m_impl->baseUrl + path;
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());

            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("POST JSON request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::post(
        const std::string &path,
        const std::string &data,
        const std::string &contentType,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            ELEGOO_LOG_DEBUG("POST request: {} - {}", path, data.substr(0, 200));

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);

            if (!contentType.empty())
            {
                std::string contentTypeHeader = "Content-Type: " + contentType;
                curlHeaders = curl_slist_append(curlHeaders, contentTypeHeader.c_str());
            }

            std::string url = m_impl->baseUrl + path;
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("POST request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::postForm(
        const std::string &path,
        const std::map<std::string, std::string> &formData,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            ELEGOO_LOG_DEBUG("POST form request: {}", path);

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);

            // Build form data string
            std::string formDataStr;
            for (const auto &[key, value] : formData)
            {
                if (!formDataStr.empty())
                {
                    formDataStr += "&";
                }
                char *escapedKey = curl_easy_escape(curl, key.c_str(), key.length());
                char *escapedValue = curl_easy_escape(curl, value.c_str(), value.length());
                formDataStr += std::string(escapedKey) + "=" + std::string(escapedValue);
                curl_free(escapedKey);
                curl_free(escapedValue);
            }

            std::string url = m_impl->baseUrl + path;
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, formDataStr.c_str());

            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("POST form request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::put(
        const std::string &path,
        const nlohmann::json &jsonData,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            std::string jsonStr = jsonData.dump();
            ELEGOO_LOG_DEBUG("PUT JSON request: {} - {}", path, jsonStr.substr(0, 200));

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);
            curlHeaders = curl_slist_append(curlHeaders, "Content-Type: application/json");

            std::string url = m_impl->baseUrl + path;
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());

            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("PUT JSON request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::put(
        const std::string &path,
        const std::string &data,
        const std::string &contentType,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            ELEGOO_LOG_DEBUG("PUT request: {} - {}", path, data.substr(0, 200));

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);

            if (!contentType.empty())
            {
                std::string contentTypeHeader = "Content-Type: " + contentType;
                curlHeaders = curl_slist_append(curlHeaders, contentTypeHeader.c_str());
            }

            std::string url = m_impl->baseUrl + path;
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("PUT request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::put(
        const std::string &path,
        const std::vector<char> &data,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout,
        const ProgressCallback &progressCallback)
    {
        try
        {
            ELEGOO_LOG_DEBUG("PUT binary request: {} - {} bytes", path, data.size());

            CURL *curl = m_impl->createCurlHandle();
            if (!curl)
            {
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "Failed to initialize curl");
            }

            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);

            std::string fullUrl = path;
            if (path.find("http://") != 0 && path.find("https://") != 0)
            {
                fullUrl = m_impl->baseUrl + path;
            }

            // Set up progress callback if provided
            std::atomic<bool> shouldCancel(false);
            struct ProgressData
            {
                ProgressCallback callback;
                std::atomic<bool> *shouldCancel;
            } progressData{progressCallback, &shouldCancel};

            if (progressCallback)
            {
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) -> int
                                 {
                                     ProgressData *pd = static_cast<ProgressData *>(clientp);
                                     if (pd->callback && ultotal > 0)
                                     {
                                         bool continueUpload = pd->callback(static_cast<uint64_t>(ulnow), static_cast<uint64_t>(ultotal));
                                         if (!continueUpload)
                                         {
                                             pd->shouldCancel->store(true);
                                             return 1; // Abort
                                         }
                                     }
                                     return 0; // Continue
                                 });
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressData);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            }

            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());

            HttpResponse response;
            std::string responseBody;

            curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Impl::headerCallback);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

            if (curlHeaders)
            {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlHeaders);
            }

            CURLcode res = curl_easy_perform(curl);

            if (curlHeaders)
            {
                curl_slist_free_all(curlHeaders);
            }

            if (shouldCancel.load())
            {
                curl_easy_cleanup(curl);
                ELEGOO_LOG_INFO("Binary PUT upload was cancelled by user");
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED,
                                                      "Upload cancelled by user");
            }

            if (res != CURLE_OK)
            {
                std::string errorMsg = curl_easy_strerror(res);
                curl_easy_cleanup(curl);

                ELEGOO_LOG_ERROR("PUT binary request failed: {}", errorMsg);

                ELINK_ERROR_CODE errorCode = ELINK_ERROR_CODE::NETWORK_ERROR;
                if (res == CURLE_OPERATION_TIMEDOUT)
                {
                    errorCode = ELINK_ERROR_CODE::OPERATION_TIMEOUT;
                }

                return BizResult<HttpResponse>::Error(errorCode, errorMsg);
            }

            long statusCode;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

            response.statusCode = static_cast<int>(statusCode);
            response.body = responseBody;

            // Set content type
            auto contentTypeIt = response.headers.find("Content-Type");
            if (contentTypeIt != response.headers.end())
            {
                response.contentType = contentTypeIt->second;
            }

            curl_easy_cleanup(curl);

            ELEGOO_LOG_DEBUG("PUT binary response: {} - {}", response.statusCode, response.body.substr(0, 500));

            return BizResult<HttpResponse>::Ok(std::move(response));
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("PUT binary request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::del(
        const std::string &path,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            ELEGOO_LOG_DEBUG("DELETE request: {}", path);

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);

            std::string url = m_impl->baseUrl + path;
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("DELETE request exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    BizResult<HttpResponse> HttpClient::del(
        const std::string &path,
        const std::string &data,
        const std::map<std::string, std::string> &headers,
        const std::optional<RequestTimeoutConfig> &timeout)
    {
        try
        {
            ELEGOO_LOG_DEBUG("DELETE request with body: {} - {}", path, data.substr(0, 200));

            CURL *curl = m_impl->createCurlHandle();
            m_impl->applyTimeout(curl, timeout);
            auto curlHeaders = m_impl->buildHeaders(headers);

            if (!data.empty())
            {
                curlHeaders = curl_slist_append(curlHeaders, "Content-Type: application/json");
            }

            std::string url = m_impl->baseUrl + path;
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

            return m_impl->performRequest(curl, url, curlHeaders);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("DELETE request with body exception: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, e.what());
        }
    }

    // Convenience method implementation
    BizResult<nlohmann::json> HttpClient::getJson(
        const std::string &path,
        const std::map<std::string, std::string> &headers)
    {
        auto result = get(path, headers);
        if (result.isError())
        {
            return BizResult<nlohmann::json>::Error(result.code, result.message);
        }

        const auto &response = result.value();
        if (!response.isSuccess())
        {
            std::string errorMsg = "HTTP error: " + std::to_string(response.statusCode);
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }

        try
        {
            return BizResult<nlohmann::json>::Ok(response.toJson());
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse JSON response: {}", e.what());
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR,
                                                    "Failed to parse JSON response: " + std::string(e.what()));
        }
    }

    BizResult<nlohmann::json> HttpClient::postJson(
        const std::string &path,
        const nlohmann::json &jsonData,
        const std::map<std::string, std::string> &headers)
    {
        auto result = post(path, jsonData, headers);
        if (result.isError())
        {
            return BizResult<nlohmann::json>::Error(result.code, result.message);
        }

        const auto &response = result.value();
        if (!response.isSuccess())
        {
            std::string errorMsg = "HTTP error: " + std::to_string(response.statusCode);
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }

        try
        {
            return BizResult<nlohmann::json>::Ok(response.toJson());
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse JSON response: {}", e.what());
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR,
                                                    "Failed to parse JSON response: " + std::string(e.what()));
        }
    }

    BizResult<nlohmann::json> HttpClient::putJson(
        const std::string &path,
        const nlohmann::json &jsonData,
        const std::map<std::string, std::string> &headers)
    {
        auto result = put(path, jsonData, headers);
        if (result.isError())
        {
            return BizResult<nlohmann::json>::Error(result.code, result.message);
        }

        const auto &response = result.value();
        if (!response.isSuccess())
        {
            std::string errorMsg = "HTTP error: " + std::to_string(response.statusCode);
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }

        try
        {
            return BizResult<nlohmann::json>::Ok(response.toJson());
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to parse JSON response: {}", e.what());
            return BizResult<nlohmann::json>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR,
                                                    "Failed to parse JSON response: " + std::string(e.what()));
        }
    }

    // Status query methods
    bool HttpClient::hasBearerToken() const
    {
        return !m_impl->bearerToken.empty();
    }

    std::string HttpClient::getBaseUrl() const
    {
        return m_impl->baseUrl;
    }

    // 请求控制方法实现
    void HttpClient::stop()
    {
        if (m_impl)
        {
            m_impl->shouldStop.store(true);
            ELEGOO_LOG_INFO("HTTP client stopped - all ongoing requests interrupted");
        }
    }

    bool HttpClient::isValid() const
    {
        return m_impl != nullptr;
    }

    BizResult<HttpResponse> HttpClient::putFile(
        const std::string &url,
        const std::string &filePath,
        const std::map<std::string, std::string> &headers,
        const ProgressCallback &progressCallback)
    {
        try
        {
            ELEGOO_LOG_DEBUG("Uploading file: {} to {}", filePath, url);

            if (!PathUtils::exists(filePath))
            {
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::FILE_NOT_FOUND,
                                                      "File not found: " + filePath);
            }

            auto fileSize = PathUtils::fileSize(filePath);
            if (fileSize == static_cast<std::uintmax_t>(-1))
            {
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::FILE_ACCESS_DENIED,
                                                      "Failed to get file size: " + filePath);
            }

            ELEGOO_LOG_INFO("File size: {} bytes", fileSize);

            CURL *curl = m_impl->createCurlHandle();
            if (!curl)
            {
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "Failed to initialize curl");
            }

            auto curlHeaders = m_impl->buildHeaders(headers);

            // Check if Content-Type header exists, if not add default
            bool hasContentType = false;
            for (const auto &[key, value] : headers)
            {
                if (key == "Content-Type")
                {
                    hasContentType = true;
                    break;
                }
            }

            if (!hasContentType)
            {
                curlHeaders = curl_slist_append(curlHeaders, "Content-Type: application/octet-stream");
            }

            // Open file for reading
            FILE *fileHandle = nullptr;
#ifdef _WIN32
            // Convert UTF-8 path to wide string on Windows
            int wchars_num = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0);
            if (wchars_num > 0)
            {
                std::vector<wchar_t> wpath(wchars_num);
                MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, wpath.data(), wchars_num);
                _wfopen_s(&fileHandle, wpath.data(), L"rb");
            }
#else
            fileHandle = fopen(filePath.c_str(), "rb");
#endif

            if (!fileHandle)
            {
                if (curlHeaders)
                    curl_slist_free_all(curlHeaders);
                curl_easy_cleanup(curl);
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::FILE_ACCESS_DENIED,
                                                      "Failed to open file: " + filePath);
            }

            // Set up progress callback if provided
            std::atomic<bool> shouldCancel(false);
            struct ProgressData
            {
                ProgressCallback callback;
                std::atomic<bool> *shouldCancel;
            } progressData{progressCallback, &shouldCancel};

            if (progressCallback)
            {
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) -> int
                                 {
                                     ProgressData *pd = static_cast<ProgressData *>(clientp);
                                     if (pd->callback && ultotal > 0)
                                     {
                                         bool continueUpload = pd->callback(static_cast<uint64_t>(ulnow), static_cast<uint64_t>(ultotal));
                                         if (!continueUpload)
                                         {
                                             pd->shouldCancel->store(true);
                                             return 1; // Abort
                                         }
                                     }
                                     return 0; // Continue
                                 });
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressData);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            }

            // Configure PUT request with file
            std::string fullUrl = url;
            if (url.find("http://") != 0 && url.find("https://") != 0)
            {
                fullUrl = m_impl->baseUrl + url;
            }

            curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_READDATA, fileHandle);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(fileSize));

            HttpResponse response;
            std::string responseBody;

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Impl::headerCallback);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

            if (curlHeaders)
            {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlHeaders);
            }

            CURLcode res = curl_easy_perform(curl);

            fclose(fileHandle);

            if (curlHeaders)
            {
                curl_slist_free_all(curlHeaders);
            }

            if (shouldCancel.load())
            {
                curl_easy_cleanup(curl);
                ELEGOO_LOG_INFO("File upload was cancelled by user");
                return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::OPERATION_CANCELLED,
                                                      "Upload cancelled by user");
            }

            if (res != CURLE_OK)
            {
                std::string errorMsg = curl_easy_strerror(res);
                curl_easy_cleanup(curl);

                ELEGOO_LOG_ERROR("File upload failed: {}", errorMsg);

                ELINK_ERROR_CODE errorCode = ELINK_ERROR_CODE::NETWORK_ERROR;
                if (res == CURLE_OPERATION_TIMEDOUT)
                {
                    errorCode = ELINK_ERROR_CODE::OPERATION_TIMEOUT;
                }

                return BizResult<HttpResponse>::Error(errorCode, "File upload failed: " + errorMsg);
            }

            long statusCode;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

            response.statusCode = static_cast<int>(statusCode);
            response.body = responseBody;

            // Set content type
            auto contentTypeIt = response.headers.find("Content-Type");
            if (contentTypeIt != response.headers.end())
            {
                response.contentType = contentTypeIt->second;
            }

            curl_easy_cleanup(curl);

            if (response.statusCode >= 200 && response.statusCode < 300)
            {
                ELEGOO_LOG_INFO("File upload completed successfully");
            }
            else
            {
                ELEGOO_LOG_ERROR("File upload failed with status code: {}", response.statusCode);
            }

            return BizResult<HttpResponse>::Ok(std::move(response));
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to upload file: {}", e.what());
            return BizResult<HttpResponse>::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR,
                                                  "Failed to upload file: " + std::string(e.what()));
        }
    }

    std::string HttpClient::urlEncode(const std::string &value)
    {
        return m_impl->urlEncode(value);
    }
} // namespace elink
