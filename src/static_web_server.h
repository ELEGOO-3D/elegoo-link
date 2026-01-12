#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <httplib.h>

namespace elink 
{
    /**
     * Static web server class using httplib to serve static files (Vue compiled web pages)
     */
    class StaticWebServer
    {
    public:
        /**
         * Constructor
         * @param port Server port (default: 3000)
         * @param host Server listen address (default: "0.0.0.0")
         */
        explicit StaticWebServer(int port = 3000, const std::string& host = "0.0.0.0");

        /**
         * Destructor
         */
        ~StaticWebServer();

        /**
         * Set the static files directory path
         * @param path Directory path containing static files (Vue build output)
         */
        void setStaticPath(const std::string& path);

        /**
         * Start the web server
         * @return true if started successfully, false otherwise
         */
        bool start();

        /**
         * Stop the web server
         */
        void stop();

        /**
         * Check if server is running
         * @return true if running, false otherwise
         */
        bool isRunning() const;

        /**
         * Get server port
         * @return Server port number
         */
        int getPort() const;

        /**
         * Get server host
         * @return Server host address
         */
        const std::string& getHost() const;

        /**
         * Set custom MIME types
         * @param extension File extension (e.g., ".vue")
         * @param mimeType MIME type (e.g., "text/html")
         */
        void setCustomMimeType(const std::string& extension, const std::string& mimeType);

        /**
         * Enable or disable directory listing for directories without index files
         * @param enable true to enable, false to disable (default: false)
         */
        void setDirectoryListing(bool enable);

        /**
         * Set custom index file names (default: index.html, index.htm)
         * @param indexFiles Vector of index file names
         */
        void setIndexFiles(const std::vector<std::string>& indexFiles);

    private:
        /**
         * Initialize server routes and handlers
         */
        void setupRoutes();

        /**
         * Get MIME type for file extension
         * @param extension File extension
         * @return MIME type string
         */
        std::string getMimeType(const std::string& extension) const;

        /**
         * Check if file exists and is readable
         * @param filepath Full file path
         * @return true if file exists and is readable
         */
        bool fileExists(const std::string& filepath) const;

        /**
         * Get file extension from filename
         * @param filename File name
         * @return File extension (including dot)
         */
        std::string getFileExtension(const std::string& filename) const;

        /**
         * Sanitize URL path to prevent directory traversal attacks
         * @param path URL path
         * @return Sanitized path
         */
        std::string sanitizePath(const std::string& path) const;

        /**
         * Find index file in directory
         * @param dirPath Directory path
         * @return Index file path if found, empty string otherwise
         */
        std::string findIndexFile(const std::string& dirPath) const;

        /**
         * Generate directory listing HTML
         * @param dirPath Directory path
         * @param urlPath URL path
         * @return HTML content for directory listing
         */
        std::string generateDirectoryListing(const std::string& dirPath, const std::string& urlPath) const;

        /**
         * Check if request should fallback to index.html for Vue routing
         * @param requestPath URL path of the request
         * @return true if should fallback to index.html, false otherwise
         */
        bool shouldFallbackToIndex(const std::string& requestPath) const;

        /**
         * Server thread function
         */
        void serverThread();

    private:
        int port_;
        std::string host_;
        std::string staticPath_;
        std::atomic<bool> running_;
        std::unique_ptr<httplib::Server> server_;
        std::unique_ptr<std::thread> serverThread_;
        
        // Configuration
        bool directoryListing_;
        std::vector<std::string> indexFiles_;
        std::unordered_map<std::string, std::string> customMimeTypes_;
        
        // Default MIME types
        static const std::unordered_map<std::string, std::string> defaultMimeTypes_;
    };

} // namespace elink
