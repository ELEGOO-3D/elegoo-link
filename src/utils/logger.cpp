#include "utils/logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/common.h>
#include <memory>
#include <iostream>
#include <filesystem>
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace elink 
{

#ifdef _WIN32
    /**
     * Set Windows console to support UTF-8 encoding
     */
    static void setupWindowsConsoleEncoding()
    {
        // Set the console code page to UTF-8
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        // Use standard locale settings
        std::locale::global(std::locale(""));

        // Ensure cout and cerr use the correct encoding
        std::cout.imbue(std::locale());
        std::cerr.imbue(std::locale());
    }
#endif

    Logger &Logger::getInstance()
    {
        static Logger instance;
        return instance;
    }

    Logger::~Logger()
    {
        // Do not call shutdown() in the destructor to avoid static destruction order issues
        // shutdown() should be explicitly called by the user or before the program ends
        if (m_initialized && m_logger)
        {
            m_initialized = false;
            try
            {
                m_logger->flush();
            }
            catch (...)
            {
                // Ignore exceptions during destruction
            }
        }
    }

    bool Logger::initialize(const LogConfig &config)
    {
        try
        {
            if (m_initialized)
            {
               return true;
            }

#ifdef _WIN32
            // Set console encoding to support UTF-8 on Windows
            // setupWindowsConsoleEncoding();
#endif

            m_config = config;
            std::vector<spdlog::sink_ptr> sinks;

            // Console output
            if (config.enableConsole)
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_pattern(config.pattern);
                sinks.push_back(console_sink);
            }

            // File output
            if (config.enableFile && !config.fileName.empty())
            {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)

                auto path = std::filesystem::u8path(config.fileName);
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        path.native(), config.maxFileSize, config.maxFiles);
                    file_sink->set_pattern(config.pattern);
                    sinks.push_back(file_sink);
#else
                // On non-Windows platforms or without SPDLOG_WCHAR_FILENAMES, use UTF-8 directly
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    config.fileName, config.maxFileSize, config.maxFiles);
                file_sink->set_pattern(config.pattern);
                sinks.push_back(file_sink);
#endif
            }

            if (sinks.empty())
            {
                // If no output is configured, default to console
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_pattern(config.pattern);
                sinks.push_back(console_sink);
            }

            // Create logger
            m_logger = std::make_shared<spdlog::logger>("elegoo_link", sinks.begin(), sinks.end());
            m_logger->set_level(toSpdlogLevel(config.level));
            m_logger->flush_on(spdlog::level::debug);

            // Register as the default logger (but do not replace if one already exists)
            try
            {
                spdlog::set_default_logger(m_logger);
            }
            catch (...)
            {
                // If setting the default logger fails, continue using our own logger
            }

            m_initialized = true;
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
            return false;
        }
    }

    void Logger::setLevel(LogLevel level)
    {
        if (m_logger)
        {
            m_config.level = level;
            m_logger->set_level(toSpdlogLevel(level));
        }
    }

    LogLevel Logger::getLevel() const
    {
        return m_config.level;
    }

    void Logger::addCallback(const LogCallback &callback)
    {
        if (callback)
        {
            m_callbacks.push_back(callback);
        }
    }

    void Logger::clearCallbacks()
    {
        m_callbacks.clear();
    }

    void Logger::flush()
    {
        if (m_logger)
        {
            m_logger->flush();
        }
    }

    void Logger::setFlushInterval(int seconds)
    {
        if (seconds <= 0)
        {
            if (m_logger)
            {
                m_logger->flush_on(spdlog::level::trace);
            }
        }
        else
        {
            spdlog::flush_every(std::chrono::seconds(seconds));
        }
    }

    bool Logger::isEnabled(LogLevel level) const
    {
        if (!m_logger)
        {
            return false;
        }
        return m_logger->should_log(toSpdlogLevel(level));
    }

    void Logger::shutdown()
    {
        if (m_initialized)
        {
            try
            {
                flush();
                clearCallbacks();

                // Clean up logger references
                if (m_logger)
                {
                    m_logger.reset();
                }

                // Only call spdlog::shutdown() when necessary
                // Note: spdlog::shutdown() cleans up all global spdlog state
                // In some cases (e.g., during static destruction), this may cause crashes
                // Therefore, we let spdlog automatically clean up at program termination
            }
            catch (const std::exception &e)
            {
                // Log the error but do not throw exceptions
                std::cerr << "Error during logger shutdown: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "Unknown error during logger shutdown" << std::endl;
            }

            m_initialized = false;
        }
    }

    void Logger::safeShutdown()
    {
        try
        {
            // Get the instance and shut it down, but do not rely on the destructor
            Logger &instance = getInstance();
            instance.shutdown();

            // Safely call spdlog::shutdown() here
            // This is an explicit call, not during destruction
            spdlog::shutdown();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error during safe shutdown: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown error during safe shutdown" << std::endl;
        }
    }

    spdlog::level::level_enum Logger::toSpdlogLevel(LogLevel level) const
    {
        switch (level)
        {
        case LogLevel::TRACE:
            return spdlog::level::trace;
        case LogLevel::DEBUG:
            return spdlog::level::debug;
        case LogLevel::INFO:
            return spdlog::level::info;
        case LogLevel::WARN:
            return spdlog::level::warn;
        case LogLevel::ERROR_LEVEL:
            return spdlog::level::err;
        case LogLevel::CRITICAL:
            return spdlog::level::critical;
        case LogLevel::OFF:
            return spdlog::level::off;
        default:
            return spdlog::level::info;
        }
    }

    LogLevel Logger::fromSpdlogLevel(spdlog::level::level_enum level) const
    {
        switch (level)
        {
        case spdlog::level::trace:
            return LogLevel::TRACE;
        case spdlog::level::debug:
            return LogLevel::DEBUG;
        case spdlog::level::info:
            return LogLevel::INFO;
        case spdlog::level::warn:
            return LogLevel::WARN;
        case spdlog::level::err:
            return LogLevel::ERROR_LEVEL;
        case spdlog::level::critical:
            return LogLevel::CRITICAL;
        case spdlog::level::off:
            return LogLevel::OFF;
        default:
            return LogLevel::INFO;
        }
    }

} // namespace elink
