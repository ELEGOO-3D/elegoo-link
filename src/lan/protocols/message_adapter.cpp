#include "protocols/message_adapter.h"
#include "utils/utils.h"
#include "utils/logger.h"
#include <iostream>
#include <sstream>
#include <random>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "types/internal/internal.h"
namespace elink 
{

    // ========== BaseMessageAdapter Implementation ==========

    BaseMessageAdapter::BaseMessageAdapter(const PrinterInfo &printerInfo)
        : printerInfo_(printerInfo), shouldStopCleanup_(false)
    {
        startCleanupTimer();
    }

    BaseMessageAdapter::~BaseMessageAdapter()
    {
        stopCleanupTimer();
    }

    std::string BaseMessageAdapter::generateMessageId() const
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);

        std::stringstream ss;
        ss << "msg_" << timestamp << "_" << dis(gen);
        return ss.str();
    }

    std::string BaseMessageAdapter::generatePrinterRequestId() const
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999); // 10000 to 99999

        auto x = dis(gen);
        return x < 0 ? std::to_string(-x) : std::to_string(x);
    }

    nlohmann::json BaseMessageAdapter::parseJson(const std::string &jsonStr) const
    {
        try
        {
            return nlohmann::json::parse(jsonStr);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("JSON parse error: {}", e.what());
            return nlohmann::json();
        }
    }

    bool BaseMessageAdapter::isValidJson(const std::string &str) const
    {
        try
        {
            auto json = nlohmann::json::parse(str);
            (void)json; // Explicitly mark as unused to avoid unused variable warning
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void BaseMessageAdapter::recordRequest(const std::string &standardMessageId, const std::string &printerRequestId, MethodType command, std::chrono::milliseconds timeout) const
    {
        std::lock_guard<std::mutex> lock(requestTrackingMutex_);

        RequestRecord record;
        record.standardMessageId = standardMessageId;
        record.printerRequestId = printerRequestId;
        record.method = command;
        record.timestamp = std::chrono::steady_clock::now();

        pendingRequests_[printerRequestId] = record;

        ELEGOO_LOG_TRACE("Recorded request mapping: {} -> {}", printerRequestId, standardMessageId);
    }

    BaseMessageAdapter::RequestRecord BaseMessageAdapter::findRequestRecord(const std::string &printerResponseId) const
    {
        std::lock_guard<std::mutex> lock(requestTrackingMutex_);

        auto it = pendingRequests_.find(printerResponseId);
        if (it != pendingRequests_.end())
        {
            return it->second;
        }

        // If direct lookup fails, try to extract ID from JSON response
        try
        {
            auto responseJson = parseJson(printerResponseId);
            if (responseJson.contains("id"))
            {
                std::string responseId = responseJson["id"];
                auto idIt = pendingRequests_.find(responseId);
                if (idIt != pendingRequests_.end())
                {
                    return idIt->second;
                }
            }
            if (responseJson.contains("id"))
            {
                std::string responseId = responseJson["id"];
                auto idIt = pendingRequests_.find(responseId);
                if (idIt != pendingRequests_.end())
                {
                    return idIt->second;
                }
            }
        }
        catch (...)
        {
            // JSON parsing failed, ignore
        }

        // Return empty record
        return RequestRecord{};
    }

    void BaseMessageAdapter::removeRequestRecord(const std::string &printerResponseId) const
    {
        std::lock_guard<std::mutex> lock(requestTrackingMutex_);
        pendingRequests_.erase(printerResponseId);
    }

    void BaseMessageAdapter::cleanupExpiredRequests()
    {
        std::lock_guard<std::mutex> lock(requestTrackingMutex_);

        auto now = std::chrono::steady_clock::now();
        int cleanedCount = 0;
        
        for (auto it = pendingRequests_.begin(); it != pendingRequests_.end();)
        {
            auto &record = it->second;
            if (now - record.timestamp > record.timeout)
            {
                ELEGOO_LOG_DEBUG("Cleaning up expired adapter request: {} -> {} (timeout: {}ms)", 
                                record.printerRequestId, record.standardMessageId, record.timeout.count());
                it = pendingRequests_.erase(it);
                cleanedCount++;
            }
            else
            {
                ++it;
            }
        }
        
        if (cleanedCount > 0)
        {
            ELEGOO_LOG_INFO("Cleaned up {} expired adapter requests for printer {}",
                           cleanedCount, StringUtils::maskString(printerInfo_.printerId));
        }
    }

    void BaseMessageAdapter::setMessageSendCallback(std::function<void(const PrinterBizRequest<std::string> &request)> callback)
    {
        messageSendCallback_ = callback;
    }

    void BaseMessageAdapter::sendMessageToPrinter(MethodType methodType, const nlohmann::json &request)
    {
        if (messageSendCallback_)
        {
            try
            {
                // Convert request to printer-specific format
                auto printerRequest = convertRequest(methodType, request, std::chrono::milliseconds(1000));
                messageSendCallback_(printerRequest);
                ELEGOO_LOG_DEBUG("Sent message to printer via callback, method type: {}", static_cast<int>(methodType));
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Error converting message for printer {}: {}", StringUtils::maskString(printerInfo_.printerId), e.what());
            }
        }
        else
        {
            ELEGOO_LOG_WARN("Message send callback not set, cannot send message to printer {}", StringUtils::maskString(printerInfo_.printerId));
        }
    }

    bool BaseMessageAdapter::hasMethodTypeRecord(MethodType methodType) const
    {
        std::lock_guard<std::mutex> lock(requestTrackingMutex_);
        
        for (const auto& pair : pendingRequests_)
        {
            if (pair.second.method == methodType)
            {
                return true;
            }
        }
        return false;
    }

    std::optional<BaseMessageAdapter::RequestRecord> BaseMessageAdapter::getOldestMethodTypeRecord(MethodType methodType) const
    {
        std::lock_guard<std::mutex> lock(requestTrackingMutex_);
        
        std::optional<RequestRecord> oldestRecord;
        std::chrono::steady_clock::time_point oldestTimestamp = std::chrono::steady_clock::time_point::max();
        
        for (const auto& pair : pendingRequests_)
        {
            if (pair.second.method == methodType)
            {
                if (pair.second.timestamp < oldestTimestamp)
                {
                    oldestTimestamp = pair.second.timestamp;
                    oldestRecord = pair.second;
                }
            }
        }
        
        return oldestRecord;
    }

    // ========== Periodic Cleanup Methods ==========

    void BaseMessageAdapter::startCleanupTimer()
    {
        shouldStopCleanup_ = false;
        cleanupThread_ = std::thread([this]()
        {
            ELEGOO_LOG_DEBUG("Adapter cleanup timer started for printer {}", StringUtils::maskString(printerInfo_.printerId));
            
            while (!shouldStopCleanup_)
            {
                try
                {
                    // Wait for specified interval or until notified to stop
                    std::unique_lock<std::mutex> lock(cleanupMutex_);
                    if (cleanupCondition_.wait_for(lock, CLEANUP_INTERVAL, [this] { return shouldStopCleanup_.load(); }))
                    {
                        // If exiting wait due to stop signal, exit loop directly
                        break;
                    }
                    
                    // Perform cleanup operation
                    if (!shouldStopCleanup_)
                    {
                        cleanupTimerCallback();
                    }
                }
                catch (const std::exception& e)
                {
                    ELEGOO_LOG_ERROR("Exception in adapter cleanup timer for printer {}: {}", 
                                    StringUtils::maskString(printerInfo_.printerId), e.what());
                    // Add slight delay when exception occurs to avoid too frequent errors
                    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                }
            }
            
            ELEGOO_LOG_DEBUG("Adapter cleanup timer stopped for printer {}", StringUtils::maskString(printerInfo_.printerId));
        });
    }

    void BaseMessageAdapter::stopCleanupTimer()
    {
        if (cleanupThread_.joinable())
        {
            ELEGOO_LOG_DEBUG("Stopping adapter cleanup timer for printer {}", StringUtils::maskString(printerInfo_.printerId));

            {
                std::lock_guard<std::mutex> lock(cleanupMutex_);
                shouldStopCleanup_ = true;
            }
            cleanupCondition_.notify_all();

            cleanupThread_.join();

            ELEGOO_LOG_DEBUG("Adapter cleanup timer stopped for printer {}", StringUtils::maskString(printerInfo_.printerId));
        }
    }

    void BaseMessageAdapter::cleanupTimerCallback()
    {
        ELEGOO_LOG_DEBUG("Running periodic adapter cleanup for printer {} (interval: {}ms)",
                         StringUtils::maskString(printerInfo_.printerId), CLEANUP_INTERVAL.count());

        cleanupExpiredRequests();
    }

    void BaseMessageAdapter::clearStatusCache() {
    }
} // namespace elink
