#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>
namespace elink
{

    /**
     * SDK version information
     */
    class SDKVersion
    {
    public:
        static constexpr int MAJOR = 1;
        static constexpr int MINOR = 0;
        static constexpr int PATCH = 0;

        static std::string getVersionString();
        static std::string getBuildInfo();
    };

    struct BroadcastInfo
    {
        std::string interfaceName;
        std::string ip;
        std::string broadcast;
    };

    /**
     * Network utility class
     */
    class NetworkUtils
    {
    public:
        static std::vector<BroadcastInfo> getBroadcastAddresses();
        /**
         * Get the list of local IP addresses
         * @return List of IP addresses
         */
        static std::vector<std::string> getLocalIPAddresses();

        static int findAvailablePort(const std::string &host);
        /**
         * Check if the IP address is valid
         * @param ip IP address
         * @return true if valid
         */
        static bool isValidIPAddress(const std::string &ip);

        /**
         * Check if the port is valid
         * @param port Port number
         * @return true if valid
         */
        static bool isValidPort(int port);

        /**
         * Create a UDP socket
         * @return Socket file descriptor, returns -1 on failure
         */
        static int createUdpSocket();

        /**
         * Set the socket to broadcast mode
         * @param socket Socket file descriptor
         * @return true if successful
         */
        static bool enableBroadcast(int socket);

        /**
         * Set socket timeout
         * @param socket Socket file descriptor
         * @param timeout_ms Timeout in milliseconds
         * @return true if successful
         */
        static bool setSocketTimeout(int socket, int timeout_ms);
    };

    /**
     * String utility class
     */
    class StringUtils
    {
    public:
        /**
         * Split a string
         * @param str Input string
         * @param delimiter Delimiter
         * @return List of split strings
         */
        static std::vector<std::string> split(const std::string &str, const std::string &delimiter);

        /**
         * Trim whitespace characters from the beginning and end of a string
         * @param str Input string
         * @return Processed string
         */
        static std::string trim(const std::string &str);

        /**
         * Convert to lowercase
         * @param str Input string
         * @return Lowercase string
         */
        static std::string toLowerCase(const std::string &str);

        /**
         * Convert to uppercase
         * @param str Input string
         * @return Uppercase string
         */
        static std::string toUpperCase(const std::string &str);

        /**
         * Check if the string starts with the specified prefix
         * @param str Input string
         * @param prefix Prefix
         * @return true if starts with prefix
         */
        static bool startsWith(const std::string &str, const std::string &prefix);

        /**
         * Check if the string ends with the specified suffix
         * @param str Input string
         * @param suffix Suffix
         * @return true if ends with suffix
         */
        static bool endsWith(const std::string &str, const std::string &suffix);

        /**
         * Replace all specified substrings in a string
         * @param str Input string
         * @param from Substring to replace
         * @param to Substring to replace with
         * @return String after replacement
         */
        static std::string replaceAll(const std::string &str, const std::string &from, const std::string &to);

        /**
         * Mask part of a string for privacy protection
         * Examples:
         *   "12345678" -> "****5678" (length 8: hide first 4, show last 4)
         *   "123456" -> "**3456" (length 6: hide first 2, show last 4)
         *   "1234" -> "1234" (length 4: show all)
         * @param str Input string
         * @param maskChar Character to use for masking (default: '*')
         * @return Masked string
         */
        static std::string maskString(const std::string &str, char maskChar = '*');

        static std::string formatErrorMessage(const std::string &message, int errorCode);
        static std::string formatErrorMessage(int errorCode, const std::string &message = "");
    };

    /**
     * File utility class
     */
    class FileUtils
    {
    public:
        /**
         * Check if a file exists
         * @param file_path File path
         * @return true if exists
         */
        static bool fileExists(const std::string &file_path);

        /**
         * Get the file size
         * @param file_path File path
         * @return File size (bytes), returns -1 on failure
         */
        static long getFileSize(const std::string &file_path);

        /**
         * Read file content
         * @param file_path File path
         * @return File content, returns an empty string on failure
         */
        static std::string readFile(const std::string &file_path);

        /**
         * Write content to a file
         * @param file_path File path
         * @param content File content
         * @return true if successful
         */
        static bool writeFile(const std::string &file_path, const std::string &content);

        /**
         * Calculate the MD5 checksum of a file
         * @param file_path File path
         * @return MD5 checksum, returns an empty string on failure
         */
        static std::string calculateMD5(const std::string &file_path);

        /**
         * Calculate the MD5 checksum of a file and encode as base64
         * First calculates the MD5 hash as a 128-bit (16-byte) binary array,
         * then encodes that binary array to base64 (not the 32-character hex string)
         * @param file_path File path
         * @return Base64-encoded MD5 hash, returns an empty string on failure
         */
        static std::string calculateMD5Base64(const std::string &file_path);

        /**
         * Get the file extension
         * @param file_path File path
         * @return File extension (without the dot)
         */
        static std::string getFileExtension(const std::string &file_path);

        /**
         * Get the file name (without the path)
         * @param file_path File path
         * @return File name
         */
        static std::string getFileName(const std::string &file_path);

        /**
         * Get the directory where the current DLL/executable is located
         * @return Directory path, returns empty string on failure
         */
        static std::string getCurrentModuleDirectory();
    };

    /**
     * UTF-8 path utility class for Windows compatibility
     */
    class PathUtils
    {
    public:
        /**
         * Open file input stream with UTF-8 path support
         * @param file_path UTF-8 encoded file path
         * @param mode File open mode
         * @return File input stream
         */
        static std::ifstream openInputStream(const std::string &file_path, std::ios_base::openmode mode = std::ios::in);

        /**
         * Open file output stream with UTF-8 path support
         * @param file_path UTF-8 encoded file path
         * @param mode File open mode
         * @return File output stream
         */
        static std::ofstream openOutputStream(const std::string &file_path, std::ios_base::openmode mode = std::ios::out);

        /**
         * Check if file exists with UTF-8 path support
         * @param file_path UTF-8 encoded file path
         * @return true if file exists
         */
        static bool exists(const std::string &file_path);

        /**
         * Get file size with UTF-8 path support
         * @param file_path UTF-8 encoded file path
         * @return File size in bytes, -1 if failed
         */
        static std::uintmax_t fileSize(const std::string &file_path);

        /**
         * Check if path is a regular file with UTF-8 path support
         * @param file_path UTF-8 encoded file path
         * @return true if path exists and is a regular file
         */
        static bool isRegularFile(const std::string &file_path);

        /**
         * Check if path is a directory with UTF-8 path support
         * @param file_path UTF-8 encoded file path
         * @return true if path exists and is a directory
         */
        static bool isDirectory(const std::string &file_path);

        /**
         * List directory contents with UTF-8 path support
         * @param dir_path UTF-8 encoded directory path
         * @return Vector of pairs containing filename and whether it's a directory
         */
        static std::vector<std::pair<std::string, bool>> listDirectory(const std::string &dir_path);
    private:
#ifdef _WIN32
        /**
         * Convert UTF-8 string to wide string (Windows only)
         * @param utf8_str UTF-8 encoded string
         * @return Wide string
         */
        static std::wstring utf8ToWide(const std::string &utf8_str);
#endif
    };

    /**
     * Time utility class
     */
    class TimeUtils
    {
    public:
        /**
         * Get the current timestamp (milliseconds)
         * @return Timestamp
         */
        static long long getCurrentTimestamp();

        /**
         * Get the current time as a string
         * @param format Time format
         * @return Time string
         */
        static std::string getCurrentTimeString(const std::string &format = "%Y-%m-%d %H:%M:%S");

        /**
         * Convert a timestamp to a time string
         * @param timestamp Timestamp (milliseconds)
         * @param format Time format
         * @return Time string
         */
        static std::string timestampToString(long long timestamp, const std::string &format = "%Y-%m-%d %H:%M:%S");

        /**
         * Calculate the time difference (milliseconds)
         * @param start Start time
         * @param end End time
         * @return Time difference (milliseconds)
         */
        static long long getTimeDifference(const std::chrono::system_clock::time_point &start,
                                           const std::chrono::system_clock::time_point &end);
    };

    /**
     * Encryption and random utility class
     */
    class CryptoUtils
    {
    public:
        /**
         * Generate a UUID string
         * @return UUID string
         */
        static std::string generateUUID();

        /**
         * Get machine unique identifier
         * This generates a unique ID based on hardware characteristics
         * @return Machine unique identifier string
         */
        static std::string getMachineId();

        /**
         * Get cached machine unique identifier
         * This method caches the machine ID to avoid regenerating it multiple times
         * @return Cached machine unique identifier string
         */
        static std::string getCachedMachineId();

        /**
         * Calculate MD5 hash of a string
         * @param input Input string to hash
         * @return MD5 hash as lowercase hexadecimal string (32 characters)
         */
        static std::string calculateMD5(const std::string &input);

        /**
         * Calculate MD5 hash of binary data
         * @param data Pointer to binary data
         * @param size Size of data in bytes
         * @return MD5 hash as lowercase hexadecimal string (32 characters)
         */
        static std::string calculateMD5(const char *data, size_t size);

        /**
         * Encode binary data to base64 string
         * @param data Pointer to binary data
         * @param size Size of data in bytes
         * @return Base64-encoded string
         */
        static std::string encodeBase64(const unsigned char *data, size_t size);

        /**
         * Calculate MD5 hash and encode as base64
         * First calculates the MD5 hash as a 128-bit (16-byte) binary array,
         * then encodes that binary array to base64 (not the 32-character hex string)
         * @param input Input string to hash
         * @return Base64-encoded MD5 hash (typically 24 characters)
         */
        static std::string calculateMD5Base64(const std::string &input);

        /**
         * Calculate MD5 hash and encode as base64
         * First calculates the MD5 hash as a 128-bit (16-byte) binary array,
         * then encodes that binary array to base64 (not the 32-character hex string)
         * @param data Pointer to binary data
         * @param size Size of data in bytes
         * @return Base64-encoded MD5 hash (typically 24 characters)
         */
        static std::string calculateMD5Base64(const char *data, size_t size);

    private:
        /**
         * Calculate MD5 hash as binary data (internal helper)
         * @param data Pointer to input data
         * @param size Size of data in bytes
         * @param hash Output buffer (must be at least 16 bytes)
         * @return true if successful, false otherwise
         */
        static bool calculateMD5Binary(const char *data, size_t size, unsigned char *hash);
    };

    /**
     * Url parsing information structure
     */
    struct UrlInfo
    {
        std::string scheme;     // http or https
        std::string host;       // hostname or IP address
        int port;              // port number, default: 80 for http, 443 for https
        std::string path;       // Url path
        std::string query;      // query string
        std::string fragment;   // fragment identifier
        bool isValid;          // whether the Url is valid
        
        UrlInfo() : port(0), isValid(false) {}
    };

    /**
     * Url utility class
     */
    class UrlUtils
    {
    public:
        /**
         * Parse Url string
         * @param Url Url string
         * @return UrlInfo structure containing parsed information
         */
        static UrlInfo parseUrl(const std::string& Url);

        static std::string extractEndpoint(const std::string& Url);
        /**
         * Extract host from Url
         * @param Url Url string
         * @return Host string, returns empty string if parsing fails
         */
        static std::string extractHost(const std::string& Url);

        /**
         * Extract port from Url
         * @param Url Url string
         * @return Port number, returns default port if not specified or parsing fails
         */
        static int extractPort(const std::string& Url);

        /**
         * Extract scheme (protocol) from Url
         * @param Url Url string
         * @return Scheme string (http/https), returns empty string if parsing fails
         */
        static std::string extractScheme(const std::string& Url);

        /**
         * Check if Url uses HTTPS
         * @param Url Url string
         * @return true if HTTPS
         */
        static bool isHTTPS(const std::string& Url);

        /**
         * Check if Url uses HTTP
         * @param Url Url string
         * @return true if HTTP
         */
        static bool isHTTP(const std::string& Url);

        /**
         * Check if Url is valid
         * @param Url Url string
         * @return true if valid HTTP/HTTPS Url
         */
        static bool isValidUrl(const std::string& Url);

        /**
         * Build Url from components
         * @param scheme Protocol (http/https)
         * @param host Hostname
         * @param port Port number (optional, use 0 for default)
         * @param path Path (optional)
         * @param query Query string (optional)
         * @return Complete Url string
         */
        static std::string buildUrl(const std::string& scheme, const std::string& host, 
                                   int port = 0, const std::string& path = "", 
                                   const std::string& query = "");

        /**
         * Url encode string
         * @param str String to encode
         * @return Url encoded string
         */
        static std::string UrlEncode(const std::string& str);

        /**
         * Url decode string
         * @param str String to decode
         * @return Url decoded string
         */
        static std::string UrlDecode(const std::string& str);

        static int getDefaultPort(const std::string& scheme);
    private:
        
        static bool isValidScheme(const std::string& scheme);
        static bool isValidHostname(const std::string& hostname);
        static std::string toLowercase(const std::string& str);
    };

} // namespace elegoo::cloud
