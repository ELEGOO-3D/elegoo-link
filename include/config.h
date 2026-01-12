#pragma once

#include <string>
#include <cstddef>
#include "version.h"
namespace elink
{
    /**
     * Unified configuration for all ElegooLink components
     * This file centralizes all configuration structures to avoid duplication
     */

    /**
     * Logging configuration (shared by all components)
     */
    struct ElegooLogConfig
    {
        int logLevel = 1;                         // Log level: 0-TRACE, 1-DEBUG, 2-INFO, 3-WARN, 4-ERROR, 5-CRITICAL, 6-OFF
        bool logEnableConsole = true;             // Enable console output
        bool logEnableFile = false;               // Enable file output
        std::string logFileName;                  // Log file name
        size_t logMaxFileSize = 10 * 1024 * 1024; // Maximum log file size (bytes)
        size_t logMaxFiles = 5;                   // Maximum number of log files
    };

    struct ElegooLocalConfig
    {
        std::string staticWebPath; // Static web files path
    };

#ifdef ENABLE_CLOUD_FEATURES
    /**
     * Network/Cloud service configuration (shared by DirectImpl and server)
     */
    struct ElegooCloudConfig
    {
        std::string region = "us"; // Region identifier, e.g., "us", "cn"
        std::string baseApiUrl;    // Base API URL, e.g. "https://api.elegoo.com"
        std::string caCertPath;    // CA certificate path for SSL/TLS verification
        std::string userAgent;     // User-Agent string
        std::string staticWebPath; // Static web files path
    };
#endif // ENABLE_CLOUD_FEATURES


    /**
     * Complete ElegooLink configuration
     * All components use this unified configuration structure
     */
    struct ElegooLinkConfig
    {
        // Shared configurations
        ElegooLogConfig log;
        ElegooLocalConfig local;
        
#ifdef ENABLE_CLOUD_FEATURES
        ElegooCloudConfig cloud;
#endif // ENABLE_CLOUD_FEATURES
    };

} // namespace elink
