#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/common.h>
#include <iostream>
#include "elegoo_export.h"
namespace elink  {

/**
 * Log level enumeration
 */
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR_LEVEL = 4,
    CRITICAL = 5,
    OFF = 6
};

/**
 * Log configuration structure
 */
struct LogConfig {
    LogLevel level = LogLevel::INFO;           // Log level
    bool enableConsole = true;                 // Whether to output to console
    bool enableFile = false;                   // Whether to output to file
    std::string fileName;                      // Log file path
    size_t maxFileSize = 10 * 1024 * 1024;    // Maximum file size (10MB)
    size_t maxFiles = 5;                       // Maximum number of files
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v"; // Log format, including thread ID, file name and line number
};

/**
 * Log callback function type
 * Parameters: level, message
 */
using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

/**
 * Log manager class
 */
class ELEGOO_LINK_API Logger {
public:
    /**
     * Get singleton instance
     */
    static Logger& getInstance();

    /**
     * Initialize the logging system
     * @param config Log configuration
     * @return true if successful
     */
    bool initialize(const LogConfig& config = LogConfig{});

    /**
     * Set log level
     * @param level Log level
     */
    void setLevel(LogLevel level);

    /**
     * Get the current log level
     */
    LogLevel getLevel() const;

    /**
     * Add a log callback function
     * @param callback Callback function
     */
    void addCallback(const LogCallback& callback);

    /**
     * Remove all callback functions
     */
    void clearCallbacks();

    /**
     * Flush the log buffer
     */
    void flush();

    /**
     * Set auto-flush interval
     * @param seconds Interval in seconds, 0 means flush on every write
     */
    void setFlushInterval(int seconds);

    /**
     * Log recording functions
     */
    template<typename... Args>
    void trace(const std::string& format, Args&&... args) {
        log(LogLevel::TRACE, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void debug(const std::string& format, Args&&... args) {
        log(LogLevel::DEBUG, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(const std::string& format, Args&&... args) {
        log(LogLevel::INFO, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warn(const std::string& format, Args&&... args) {
        log(LogLevel::WARN, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(const std::string& format, Args&&... args) {
        log(LogLevel::ERROR_LEVEL, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void critical(const std::string& format, Args&&... args) {
        log(LogLevel::CRITICAL, format, std::forward<Args>(args)...);
    }

    /**
     * Log recording functions with file location information
     */
    template<typename... Args>
    void traceWithLocation(const char* file, int line, const std::string& format, Args&&... args) {
        logWithLocation(LogLevel::TRACE, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void debugWithLocation(const char* file, int line, const std::string& format, Args&&... args) {
        logWithLocation(LogLevel::DEBUG, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void infoWithLocation(const char* file, int line, const std::string& format, Args&&... args) {
        logWithLocation(LogLevel::INFO, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warnWithLocation(const char* file, int line, const std::string& format, Args&&... args) {
        logWithLocation(LogLevel::WARN, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void errorWithLocation(const char* file, int line, const std::string& format, Args&&... args) {
        logWithLocation(LogLevel::ERROR_LEVEL, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void criticalWithLocation(const char* file, int line, const std::string& format, Args&&... args) {
        logWithLocation(LogLevel::CRITICAL, file, line, format, std::forward<Args>(args)...);
    }

    /**
     * Check if the specified log level is enabled
     */
    bool isEnabled(LogLevel level) const;

    /**
     * Shut down the logging system
     */
    void shutdown();

    /**
     * Safely shut down the logging system (static method)
     * This method can be safely called before the program ends to avoid destructor order issues
     */
    static void safeShutdown();

private:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    template<typename... Args>
    void log(LogLevel level, const std::string& format, Args&&... args) {
        if (!m_logger || !m_initialized || !isEnabled(level)) {
            return;
        }

        try {
            auto spdLevel = toSpdlogLevel(level);
            std::string message;
            
            if constexpr (sizeof...(args) > 0) {
                message = fmt::format(format, std::forward<Args>(args)...);
            } else {
                message = format;
            }

            m_logger->log(spdLevel, message);

            // Call callback functions
            for (const auto& callback : m_callbacks) {
                try {
                    callback(level, message);
                } catch (...) {
                    // Ignore exceptions in callback functions to avoid affecting the logging system
                }
            }
        } catch (const std::exception& e) {
            // Output to stderr on exception to avoid infinite recursion
            std::cerr << "Logger error: " << e.what() << std::endl;
        } catch (...) {
            // std::cerr << "Unknown logger error" << std::endl;
        }
    }

    template<typename... Args>
    void logWithLocation(LogLevel level, const char* file, int line, const std::string& format, Args&&... args) {
        if (!m_logger || !m_initialized || !isEnabled(level)) {
            return;
        }

        try {
            auto spdLevel = toSpdlogLevel(level);
            std::string message;
            
            if constexpr (sizeof...(args) > 0) {
                message = fmt::format(format, std::forward<Args>(args)...);
            } else {
                message = format;
            }

            // Use spdlog's source location feature
            spdlog::source_loc loc{file, line, ""};
            m_logger->log(loc, spdLevel, message);

            // Call callback functions
            for (const auto& callback : m_callbacks) {
                try {
                    callback(level, message);
                } catch (...) {
                    // Ignore exceptions in callback functions to avoid affecting the logging system
                }
            }
        } catch (const std::exception& e) {
            // Output to stderr on exception to avoid infinite recursion
            std::cerr << "Logger error: " << e.what() << std::endl;
        } catch (...) {
            // std::cerr << "Unknown logger error" << std::endl;
        }
    }

    spdlog::level::level_enum toSpdlogLevel(LogLevel level) const;
    LogLevel fromSpdlogLevel(spdlog::level::level_enum level) const;

    std::shared_ptr<spdlog::logger> m_logger;
    std::vector<LogCallback> m_callbacks;
    LogConfig m_config;
    bool m_initialized = false;
};

// /**
//  * Convenient macro definitions
//  */
// #define ELEGOO_LOG_TRACE(...)    elink::Logger::getInstance().trace(__VA_ARGS__)
// #define ELEGOO_LOG_DEBUG(...)    elink::Logger::getInstance().debug(__VA_ARGS__)
// #define ELEGOO_LOG_INFO(...)     elink::Logger::getInstance().info(__VA_ARGS__)
// #define ELEGOO_LOG_WARN(...)     elink::Logger::getInstance().warn(__VA_ARGS__)
// #define ELEGOO_LOG_ERROR(...)    elink::Logger::getInstance().error(__VA_ARGS__)
// #define ELEGOO_LOG_CRITICAL(...) elink::Logger::getInstance().critical(__VA_ARGS__)

/**
 * Log macros with file location information
 */
#define ELEGOO_LOG_TRACE(...)    elink::Logger::getInstance().traceWithLocation(__FILE__, __LINE__, __VA_ARGS__)
#define ELEGOO_LOG_DEBUG(...)    elink::Logger::getInstance().debugWithLocation(__FILE__, __LINE__, __VA_ARGS__)
#define ELEGOO_LOG_INFO(...)     elink::Logger::getInstance().infoWithLocation(__FILE__, __LINE__, __VA_ARGS__)
#define ELEGOO_LOG_WARN(...)     elink::Logger::getInstance().warnWithLocation(__FILE__, __LINE__, __VA_ARGS__)
#define ELEGOO_LOG_ERROR(...)    elink::Logger::getInstance().errorWithLocation(__FILE__, __LINE__, __VA_ARGS__)
#define ELEGOO_LOG_CRITICAL(...) elink::Logger::getInstance().criticalWithLocation(__FILE__, __LINE__, __VA_ARGS__)

/**
 * Conditional log macros
 */
#define ELEGOO_LOG_TRACE_IF(condition, ...)    if (condition) ELEGOO_LOG_TRACE(__VA_ARGS__)
#define ELEGOO_LOG_DEBUG_IF(condition, ...)    if (condition) ELEGOO_LOG_DEBUG(__VA_ARGS__)
#define ELEGOO_LOG_INFO_IF(condition, ...)     if (condition) ELEGOO_LOG_INFO(__VA_ARGS__)
#define ELEGOO_LOG_WARN_IF(condition, ...)     if (condition) ELEGOO_LOG_WARN(__VA_ARGS__)
#define ELEGOO_LOG_ERROR_IF(condition, ...)    if (condition) ELEGOO_LOG_ERROR(__VA_ARGS__)
#define ELEGOO_LOG_CRITICAL_IF(condition, ...) if (condition) ELEGOO_LOG_CRITICAL(__VA_ARGS__)

} // namespace elink
