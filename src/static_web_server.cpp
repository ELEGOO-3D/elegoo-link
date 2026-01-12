#include "static_web_server.h"
#include "utils/logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include "utils/utils.h"
namespace elink 
{
    // Default MIME types mapping
    const std::unordered_map<std::string, std::string> StaticWebServer::defaultMimeTypes_ = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".eot", "application/vnd.ms-fontobject"},
        {".otf", "font/otf"},
        {".xml", "application/xml"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".txt", "text/plain; charset=utf-8"},
        {".map", "application/json"},
        {".vue", "text/html; charset=utf-8"} // Vue files as HTML
    };

    StaticWebServer::StaticWebServer(int port, const std::string& host)
        : port_(port), host_(host), running_(false), directoryListing_(false)
    {
        // Default index files
        indexFiles_ = {"index.html", "index.htm", "default.html", "default.htm","index"};
        
        ELEGOO_LOG_INFO("StaticWebServer created on {}:{}", host_, port_);
    }

    StaticWebServer::~StaticWebServer()
    {
        stop();
    }

    void StaticWebServer::setStaticPath(const std::string& path)
    {
        staticPath_ = path;
        ELEGOO_LOG_INFO("Static path set to: {}", staticPath_);
    }

    bool StaticWebServer::start()
    {
        if (running_.load()) {
            ELEGOO_LOG_WARN("StaticWebServer is already running");
            return true;
        }

        if (staticPath_.empty()) {
            ELEGOO_LOG_ERROR("Static path not set. Use setStaticPath() first.");
            return false;
        }

        if (!PathUtils::exists(staticPath_)) {
            ELEGOO_LOG_ERROR("Static path does not exist: {}", staticPath_);
            return false;
        }

        server_ = std::make_unique<httplib::Server>();
        setupRoutes();

        running_.store(true);
        serverThread_ = std::make_unique<std::thread>(&StaticWebServer::serverThread, this);

        //sleep 10 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        ELEGOO_LOG_INFO("StaticWebServer started on {}:{}", host_, port_);
        return true;
    }

    void StaticWebServer::stop()
    {
        if (!running_.load()) {
            return;
        }

        running_.store(false);
        
        if (server_) {
            server_->stop();
        }

        if (serverThread_ && serverThread_->joinable()) {
            serverThread_->join();
        }

        server_.reset();
        serverThread_.reset();

        ELEGOO_LOG_INFO("StaticWebServer stopped");
    }

    bool StaticWebServer::isRunning() const
    {
        return running_.load();
    }

    int StaticWebServer::getPort() const
    {
        return port_;
    }

    const std::string& StaticWebServer::getHost() const
    {
        return host_;
    }

    void StaticWebServer::setCustomMimeType(const std::string& extension, const std::string& mimeType)
    {
        customMimeTypes_[extension] = mimeType;
        ELEGOO_LOG_DEBUG("Custom MIME type set: {} -> {}", extension, mimeType);
    }

    void StaticWebServer::setDirectoryListing(bool enable)
    {
        directoryListing_ = enable;
        ELEGOO_LOG_DEBUG("Directory listing {}", enable ? "enabled" : "disabled");
    }

    void StaticWebServer::setIndexFiles(const std::vector<std::string>& indexFiles)
    {
        indexFiles_ = indexFiles;
        ELEGOO_LOG_DEBUG("Index files updated");
    }

    void StaticWebServer::setupRoutes()
    {
        // Handle all requests with a catch-all route
        server_->Get(R"(.*)", [this](const httplib::Request& req, httplib::Response& res) {
            std::string requestPath = sanitizePath(req.path);
            std::string fullPath = staticPath_ + requestPath;

            try {
                if (PathUtils::exists(fullPath)) {
                    if (PathUtils::isDirectory(fullPath)) {
                        // Try to find index file
                        std::string indexPath = findIndexFile(fullPath);
                        if (!indexPath.empty()) {
                            fullPath = indexPath;
                        } else if (directoryListing_) {
                            // Generate directory listing
                            std::string listingHtml = generateDirectoryListing(fullPath, requestPath);
                            res.set_content(listingHtml, "text/html; charset=utf-8");
                            return;
                        } else {
                            res.status = 403;
                            res.set_content("<h1>403 Forbidden</h1><p>Directory listing is disabled.</p>", "text/html");
                            return;
                        }
                    }

                    if (PathUtils::isRegularFile(fullPath)) {
                        // Read file content using PathUtils for UTF-8 support
                        auto file = PathUtils::openInputStream(fullPath, std::ios::binary);
                        if (file.is_open()) {
                            std::stringstream buffer;
                            buffer << file.rdbuf();
                            std::string content = buffer.str();
                            
                            // Set appropriate MIME type
                            std::string filename = fullPath.substr(fullPath.find_last_of("/\\") + 1);
                            std::string extension = getFileExtension(filename);
                            std::string mimeType = getMimeType(extension);
                            
                            res.set_content(content, mimeType);
                            
                            // Set cache headers for static files
                            if (extension != ".html" && extension != ".htm") {
                                res.set_header("Cache-Control", "public, max-age=3600");
                            }
                            
                            ELEGOO_LOG_DEBUG("Served file: {} ({})", fullPath, mimeType);
                            return;
                        } else {
                            ELEGOO_LOG_ERROR("Failed to open file: {}", fullPath);
                            res.status = 500;
                            res.set_content("<h1>500 Internal Server Error</h1><p>Failed to open file.</p>", "text/html");
                            return;
                        }
                    }
                }

                // File not found - check if this should fallback to index.html for Vue routing
                if (shouldFallbackToIndex(requestPath)) {
                    // Try to serve index.html for Vue client-side routing
                    std::string indexPath = findIndexFile(staticPath_);
                    if (!indexPath.empty()) {
                        // Use PathUtils for UTF-8 support
                        auto file = PathUtils::openInputStream(indexPath, std::ios::binary);
                        if (file.is_open()) {
                            std::stringstream buffer;
                            buffer << file.rdbuf();
                            std::string content = buffer.str();
                            res.set_content(content, "text/html; charset=utf-8");
                            ELEGOO_LOG_DEBUG("Served index.html for Vue route: {}", requestPath);
                            return;
                        }
                    }
                }
                
                // File not found or not accessible
                res.status = 404;
                res.set_content("<h1>404 Not Found</h1><p>The requested resource was not found.</p>", "text/html");
                ELEGOO_LOG_DEBUG("File not found: {}", fullPath);

            } catch (const std::exception& e) {
                ELEGOO_LOG_ERROR("Error serving file {}: {}", fullPath, e.what());
                res.status = 500;
                res.set_content("<h1>500 Internal Server Error</h1>", "text/html");
            }
        });

        // Add CORS headers for API compatibility
        server_->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            return httplib::Server::HandlerResponse::Unhandled;
        });

        // Handle OPTIONS requests for CORS
        server_->Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
            res.status = 200;
        });
    }

    std::string StaticWebServer::getMimeType(const std::string& extension) const
    {
        // Check custom MIME types first
        auto it = customMimeTypes_.find(extension);
        if (it != customMimeTypes_.end()) {
            return it->second;
        }

        // Check default MIME types
        auto defaultIt = defaultMimeTypes_.find(extension);
        if (defaultIt != defaultMimeTypes_.end()) {
            return defaultIt->second;
        }

        // Default to binary
        return "application/octet-stream";
    }

    bool StaticWebServer::fileExists(const std::string& filepath) const
    {
        return PathUtils::isRegularFile(filepath);
    }

    std::string StaticWebServer::getFileExtension(const std::string& filename) const
    {
        size_t pos = filename.find_last_of('.');
        if (pos != std::string::npos) {
            std::string ext = filename.substr(pos);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext;
        }
        return "";
    }

    std::string StaticWebServer::sanitizePath(const std::string& path) const
    {
        std::string sanitized = path;
        
        // Replace backslashes with forward slashes
        std::replace(sanitized.begin(), sanitized.end(), '\\', '/');
        
        // Remove double slashes
        std::regex doubleSlash(R"(/+)");
        sanitized = std::regex_replace(sanitized, doubleSlash, "/");
        
        // Remove relative path components
        std::regex relativePath(R"(/\.\./)");
        while (std::regex_search(sanitized, relativePath)) {
            sanitized = std::regex_replace(sanitized, relativePath, "/");
        }
        
        // Remove trailing dots and slashes that could be used for directory traversal
        std::regex trailingDots(R"(/\.\./?)");
        sanitized = std::regex_replace(sanitized, trailingDots, "/");
        
        // Ensure path starts with /
        if (sanitized.empty() || sanitized[0] != '/') {
            sanitized = "/" + sanitized;
        }
        
        return sanitized;
    }

    std::string StaticWebServer::findIndexFile(const std::string& dirPath) const
    {
        try {
            for (const auto& indexFile : indexFiles_) {
                // Manually construct path instead of using std::filesystem::path
                std::string indexPathStr = dirPath;
                if (!indexPathStr.empty() && indexPathStr.back() != '/' && indexPathStr.back() != '\\') {
                    indexPathStr += "/";
                }
                indexPathStr += indexFile;
                
                if (PathUtils::isRegularFile(indexPathStr)) {
                    ELEGOO_LOG_DEBUG("Found index file: {}", indexPathStr);
                    return indexPathStr;
                }
            }
        } catch (const std::exception& e) {
            ELEGOO_LOG_ERROR("Error in findIndexFile for {}: {}", dirPath, e.what());
        }
        return "";
    }

    std::string StaticWebServer::generateDirectoryListing(const std::string& dirPath, const std::string& urlPath) const
    {
        std::stringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html><head><title>Directory Listing: " << urlPath << "</title>\n";
        html << "<style>\n";
        html << "body { font-family: Arial, sans-serif; margin: 40px; }\n";
        html << "h1 { color: #333; }\n";
        html << "a { text-decoration: none; color: #0066cc; }\n";
        html << "a:hover { text-decoration: underline; }\n";
        html << ".file { margin: 5px 0; }\n";
        html << ".dir { font-weight: bold; }\n";
        html << "</style></head><body>\n";
        html << "<h1>Directory Listing: " << urlPath << "</h1>\n";
        
        // Parent directory link
        if (urlPath != "/") {
            std::string parentPath = urlPath;
            if (parentPath.back() == '/') {
                parentPath.pop_back();
            }
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos) {
                parentPath = parentPath.substr(0, lastSlash + 1);
            } else {
                parentPath = "/";
            }
            html << "<div class='file'><a href='" << parentPath << "'>..</a></div>\n";
        }

        try {
            auto entries = PathUtils::listDirectory(dirPath);
            for (const auto& entry : entries) {
                const std::string& filename = entry.first;
                bool isDir = entry.second;
                
                std::string linkPath = urlPath;
                if (linkPath.back() != '/') {
                    linkPath += "/";
                }
                linkPath += filename;

                if (isDir) {
                    html << "<div class='file dir'><a href='" << linkPath << "/'>" << filename << "/</a></div>\n";
                } else {
                    html << "<div class='file'><a href='" << linkPath << "'>" << filename << "</a></div>\n";
                }
            }
        } catch (const std::exception& e) {
            html << "<p>Error reading directory: " << e.what() << "</p>\n";
        }

        html << "</body></html>";
        return html.str();
    }

    bool StaticWebServer::shouldFallbackToIndex(const std::string& requestPath) const
    {
        // Don't fallback for requests that look like static resources
        std::string extension = getFileExtension(requestPath);
        
        // List of extensions that should not fallback to index.html
        static const std::vector<std::string> staticExtensions = {
            ".js", ".css", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".ico",
            ".woff", ".woff2", ".ttf", ".eot", ".otf", ".xml", ".pdf", 
            ".zip", ".txt", ".map", ".json"
        };
        
        // If request has a static file extension, don't fallback
        if (!extension.empty()) {
            for (const auto& staticExt : staticExtensions) {
                if (extension == staticExt) {
                    return false;
                }
            }
        }
        
        // Don't fallback for API requests (assuming they start with /api)
        if (requestPath.find("/api/") == 0) {
            return false;
        }
        
        // For all other requests (likely Vue routes), fallback to index.html
        return true;
    }

    void StaticWebServer::serverThread()
    {
        try {
            ELEGOO_LOG_INFO("Starting StaticWebServer thread on {}:{}", host_, port_);
            if(running_.load() == false) {
                ELEGOO_LOG_WARN("StaticWebServer thread exiting because server is not running");
                return;
            }
            if (!server_->listen(host_, port_)) {
                ELEGOO_LOG_ERROR("Failed to start StaticWebServer on {}:{}", host_, port_);
                running_.store(false);
                return;
            }
        } catch (const std::exception& e) {
            ELEGOO_LOG_ERROR("StaticWebServer thread error: {}", e.what());
            running_.store(false);
        } catch (...) {
            ELEGOO_LOG_ERROR("StaticWebServer thread unknown error");
            running_.store(false);
        }
    }

} // namespace elink
