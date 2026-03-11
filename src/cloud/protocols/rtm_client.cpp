#include "protocols/rtm_client.h"
#include "utils/logger.h"
#include "types/biz.h"

// // Agora RTM SDK headers
// #include "IAgoraRtmClient.h"

// Standard library includes
#include <map>
#include <set>
#include <atomic>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <utils/utils.h>
using namespace agora::rtm;

namespace elink
{
    // ==================== RtmEventHandler Implementation ====================

    class RtmEventHandler : public IRtmEventHandler
    {
    public:
        RtmEventHandler() = default;
        virtual ~RtmEventHandler() = default;

        void resetConnectionState()
        {
            std::lock_guard<std::mutex> lock(connectionMutex_);
            currentConnectionState_ = RTM_LINK_STATE_DISCONNECTED;
            currentConnectionChangeReason_ = RTM_LINK_STATE_CHANGE_REASON_UNKNOWN;
            connectionCompleted_ = false;
        }
        void resetLoginState()
        {
            std::lock_guard<std::mutex> lock(loginMutex_);
            loginCompleted_ = false;
            pendingLoginRequestId_ = 0;
            loginResult_ = VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Unknown error");
        }
        // Set callback functions
        void setMessageCallback(const RtmMessageCallback &callback)
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            messageCallback_ = callback;
        }

        void setPresenceCallback(const RtmPresenceCallback &callback)
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            presenceCallback_ = callback;
        }

        void setConnectionStateCallback(const RtmConnectionStateCallback &callback)
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            connectionStateCallback_ = callback;
        }

        // Clean up expired states (states older than maxAge seconds)
        void cleanupExpiredStates(int maxAgeSeconds = 20)
        {
            auto now = std::chrono::steady_clock::now();
            auto maxAge = std::chrono::seconds(maxAgeSeconds);

            {
                std::lock_guard<std::mutex> lock(subscribeMutex_);
                for (auto it = subscribeStates_.begin(); it != subscribeStates_.end();)
                {
                    if ((now - it->second.timestamp) > maxAge)
                    {
                        it = subscribeStates_.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(unsubscribeMutex_);
                for (auto it = unsubscribeStates_.begin(); it != unsubscribeStates_.end();)
                {
                    if ((now - it->second.timestamp) > maxAge)
                    {
                        it = unsubscribeStates_.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(publishMutex_);
                for (auto it = publishStates_.begin(); it != publishStates_.end();)
                {
                    if ((now - it->second.timestamp) > maxAge)
                    {
                        it = publishStates_.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }

        VoidResult rtmErrorCodeToNetworkErrorCode(RTM_ERROR_CODE rtmError)
        {
            ELINK_ERROR_CODE networkErrorCode;
            std::string errorMessage = getErrorReason(rtmError);
            switch (rtmError)
            {
            case RTM_ERROR_OK:
                networkErrorCode = ELINK_ERROR_CODE::SUCCESS;
                break;
            case RTM_ERROR_CHANNEL_RECEIVER_OFFLINE:
                networkErrorCode = ELINK_ERROR_CODE::PRINTER_OFFLINE;
                errorMessage = StringUtils::formatErrorMessage(rtmError, errorMessage);
                break;
            default:
                networkErrorCode = ELINK_ERROR_CODE::UNKNOWN_ERROR;
                errorMessage = StringUtils::formatErrorMessage(rtmError, errorMessage);
            }
            return VoidResult::Error(networkErrorCode, errorMessage);
        }

        // Wait for login result
        VoidResult waitForLoginResult(uint64_t requestId, int timeoutSeconds = 15)
        {
            std::unique_lock<std::mutex> lock(loginMutex_);
            pendingLoginRequestId_ = requestId;

            if (loginCondition_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this]
                                         { return loginCompleted_; }))
            {
                return loginResult_;
            }
            else
            {
                return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "Login timeout");
            }
        }

        // Wait for connection state change
        bool waitForConnectionState(RTM_LINK_STATE expectedState, int timeoutSeconds = 15)
        {
            std::unique_lock<std::mutex> lock(connectionMutex_);

            if (connectionCondition_.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                                              [this, expectedState]
                                              {
                                                  return connectionCompleted_ && currentConnectionState_ == expectedState;
                                              }))
            {
                return true;
            }
            else
            {
                return false;
            }
        }

        // Get current connection change reason
        RTM_LINK_STATE_CHANGE_REASON getCurrentConnectionChangeReason() const
        {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(connectionMutex_));
            return currentConnectionChangeReason_;
        }

        RTM_LINK_STATE getCurrentConnectionState() const
        {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(connectionMutex_));
            return currentConnectionState_;
        }
        // Wait for subscribe result
        bool waitForSubscribeResult(uint64_t requestId, std::string &errorMessage, int timeoutSeconds = 10)
        {
            std::unique_lock<std::mutex> lock(subscribeMutex_);

            // Wait for state to be created by callback
            if (subscribeCondition_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this, requestId]
                                             { return subscribeStates_.find(requestId) != subscribeStates_.end(); }))
            {
                auto it = subscribeStates_.find(requestId);
                if (it != subscribeStates_.end())
                {
                    errorMessage = it->second.errorMessage;
                    bool success = it->second.success;
                    subscribeStates_.erase(it);
                    return success;
                }
            }

            // Timeout: don't delete state, let callback cleanup or periodic cleanup handle it
            errorMessage = "Subscribe timeout";
            return false;
        }

        // Wait for unsubscribe result
        bool waitForUnsubscribeResult(uint64_t requestId, std::string &errorMessage, int timeoutSeconds = 10)
        {
            std::unique_lock<std::mutex> lock(unsubscribeMutex_);

            // Wait for state to be created by callback
            if (unsubscribeCondition_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this, requestId]
                                               { return unsubscribeStates_.find(requestId) != unsubscribeStates_.end(); }))
            {
                auto it = unsubscribeStates_.find(requestId);
                if (it != unsubscribeStates_.end())
                {
                    errorMessage = it->second.errorMessage;
                    bool success = it->second.success;
                    unsubscribeStates_.erase(it);
                    return success;
                }
            }

            // Timeout: don't delete state, let callback cleanup or periodic cleanup handle it
            errorMessage = "Unsubscribe timeout";
            return false;
        }

        // Wait for publish result
        VoidResult waitForPublishResult(uint64_t requestId, int timeoutSeconds = 10)
        {
            std::unique_lock<std::mutex> lock(publishMutex_);

            // Wait for state to be created by callback
            if (publishCondition_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this, requestId]
                                           { return publishStates_.find(requestId) != publishStates_.end(); }))
            {
                auto it = publishStates_.find(requestId);
                if (it != publishStates_.end())
                {
                    VoidResult result = it->second.result;
                    publishStates_.erase(it);
                    return result;
                }
            }

            // Timeout: don't delete state, let callback cleanup or periodic cleanup handle it
            return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "Publish timeout");
        }

        // Connection state change event (replaces deprecated onConnectionStateChanged)
        void onLinkStateEvent(const LinkStateEvent &event) override
        {
            {
                std::lock_guard<std::mutex> lock(connectionMutex_);
                currentConnectionState_ = event.currentState;
                currentConnectionChangeReason_ = event.reasonCode;
                connectionCompleted_ = true;
            }
            connectionCondition_.notify_all();

            // Build affected channels string
            std::string affectedChannelsStr;
            for (size_t i = 0; i < event.affectedChannelCount; ++i)
            {
                if (i > 0) affectedChannelsStr += ", ";
                if (event.affectedChannels[i]) affectedChannelsStr += event.affectedChannels[i];
            }

            // Build unrestored channels string
            std::string unrestoredChannelsStr;
            for (size_t i = 0; i < event.unrestoredChannelCount; ++i)
            {
                if (i > 0) unrestoredChannelsStr += ", ";
                if (event.unrestoredChannels[i]) unrestoredChannelsStr += event.unrestoredChannels[i];
            }

            ELEGOO_LOG_INFO(
                "[RTM] Link state event: {} -> {}, serviceType={}, operation={}, reasonCode={}, reason={}, "
                "isResumed={}, affectedChannels=[{}], unrestoredChannels=[{}], timestamp={}",
                static_cast<int>(event.previousState),
                static_cast<int>(event.currentState),
                static_cast<int>(event.serviceType),
                static_cast<int>(event.operation),
                static_cast<int>(event.reasonCode),
                event.reason ? event.reason : "",
                event.isResumed,
                affectedChannelsStr,
                unrestoredChannelsStr,
                event.timestamp);

            // Call connection state callback
            RtmConnectionStateCallback connectionStateCallback;
            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                connectionStateCallback = connectionStateCallback_;
            }

            if (connectionStateCallback)
            {
                connectionStateCallback(event.currentState, event.reasonCode);
            }
        }

        // Message receive event
        void onMessageEvent(const MessageEvent &event) override
        {
            RtmMessageCallback callback;
            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                callback = messageCallback_;
            }

            if (callback)
            {
                RtmMessage rtmMessage;
                rtmMessage.channelName = event.channelName ? event.channelName : "";
                rtmMessage.publisher = event.publisher ? event.publisher : "";
                rtmMessage.content = std::string(static_cast<const char *>(event.message), event.messageLength);
                rtmMessage.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count();
                callback(rtmMessage);
            }
        }

        // Subscribe result event
        void onSubscribeResult(const uint64_t requestId, const char *channelName, RTM_ERROR_CODE errorCode) override
        {
            {
                std::lock_guard<std::mutex> lock(subscribeMutex_);
                // Create state directly in callback
                SubscribeState &state = subscribeStates_[requestId];
                state.success = (errorCode == RTM_ERROR_OK);
                state.errorMessage = errorCode == RTM_ERROR_OK ? "Subscribe success" : "Subscribe failed with error code: " + std::to_string(static_cast<int>(errorCode));
            }
            subscribeCondition_.notify_all();

            ELEGOO_LOG_TRACE("[RTM] Subscribe result for channel {}: {}", channelName ? channelName : "",
                             errorCode == RTM_ERROR_OK ? "Success" : "Failed");
        }

        // Unsubscribe result event
        void onUnsubscribeResult(const uint64_t requestId, const char *channelName, RTM_ERROR_CODE errorCode) override
        {
            {
                std::lock_guard<std::mutex> lock(unsubscribeMutex_);
                // Create state directly in callback
                UnsubscribeState &state = unsubscribeStates_[requestId];
                state.success = (errorCode == RTM_ERROR_OK);
                state.errorMessage = errorCode == RTM_ERROR_OK ? "Unsubscribe success" : "Unsubscribe failed with error code: " + std::to_string(static_cast<int>(errorCode));
            }
            unsubscribeCondition_.notify_all();

            ELEGOO_LOG_TRACE("[RTM] Unsubscribe result for channel {}: {}", channelName ? channelName : "",
                             errorCode == RTM_ERROR_OK ? "Success" : "Failed");
        }

        // Publish result event
        void onPublishResult(const uint64_t requestId, RTM_ERROR_CODE errorCode) override
        {
            {
                std::lock_guard<std::mutex> lock(publishMutex_);
                // Create state directly in callback
                PublishState &state = publishStates_[requestId];
                state.result = rtmErrorCodeToNetworkErrorCode(errorCode);
            }
            publishCondition_.notify_all();

            ELEGOO_LOG_TRACE("[RTM] Publish result: {}",
                             errorCode == RTM_ERROR_OK ? "Success" : "Failed");
            if (errorCode == RTM_ERROR_NOT_CONNECTED)
            {
                std::lock_guard<std::mutex> lock(connectionMutex_);
                currentConnectionState_ = RTM_LINK_STATE_DISCONNECTED;
            }
        }

        // Login result event
        void onLoginResult(const uint64_t requestId, RTM_ERROR_CODE errorCode) override
        {
            {
                std::lock_guard<std::mutex> lock(loginMutex_);
                if (requestId == pendingLoginRequestId_)
                {
                    loginResult_ = rtmErrorCodeToNetworkErrorCode(errorCode);
                    loginCompleted_ = true;
                }
            }
            loginCondition_.notify_all();
        }

    private:
        // Callback function protection
        mutable std::mutex callbackMutex_;
        RtmMessageCallback messageCallback_;
        RtmPresenceCallback presenceCallback_;
        RtmConnectionStateCallback connectionStateCallback_;

        // Login sync wait related
        std::mutex loginMutex_;
        std::condition_variable loginCondition_;
        uint64_t pendingLoginRequestId_ = 0;
        bool loginCompleted_ = false;
        VoidResult loginResult_;

        // Connection state sync wait related
        std::mutex connectionMutex_;
        std::condition_variable connectionCondition_;
        bool connectionCompleted_ = false;
        RTM_LINK_STATE currentConnectionState_ = RTM_LINK_STATE_DISCONNECTED;
        RTM_LINK_STATE_CHANGE_REASON currentConnectionChangeReason_ = RTM_LINK_STATE_CHANGE_REASON_UNKNOWN;

        // State structures for concurrent requests
        struct SubscribeState
        {
            bool success = false;
            std::string errorMessage;
            std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
        };

        struct UnsubscribeState
        {
            bool success = false;
            std::string errorMessage;
            std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
        };

        struct PublishState
        {
            VoidResult result;
            std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
        };

        // Subscribe sync wait related - support concurrent requests
        std::mutex subscribeMutex_;
        std::condition_variable subscribeCondition_;
        std::map<uint64_t, SubscribeState> subscribeStates_;

        // Unsubscribe sync wait related - support concurrent requests
        std::mutex unsubscribeMutex_;
        std::condition_variable unsubscribeCondition_;
        std::map<uint64_t, UnsubscribeState> unsubscribeStates_;

        // Publish sync wait related - support concurrent requests
        std::mutex publishMutex_;
        std::condition_variable publishCondition_;
        std::map<uint64_t, PublishState> publishStates_;
    };

    // ==================== RtmClient::Impl Implementation ====================

    RtmClient::RtmClient(const RtmConfig &config)
        : config_(config), eventHandler_(std::make_unique<RtmEventHandler>()), rtmClient_(nullptr), isLoggedIn_(false), isShutdown_(false)
    {
        initialize();
    }

    RtmClient::~RtmClient()
    {
        cleanup();
    }

    void RtmClient::initialize()
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);

        try
        {
            // If client already exists, clean up first
            if (rtmClient_)
            {
                rtmClient_->release();
                rtmClient_ = nullptr;
            }

            // Create Agora RTM configuration
            agora::rtm::RtmConfig agoraConfig;
            agoraConfig.appId = config_.appId.c_str();
            agoraConfig.userId = config_.userId.c_str();
            agoraConfig.eventHandler = eventHandler_.get();
            agoraConfig.presenceTimeout = config_.presenceTimeout;
            agoraConfig.heartbeatInterval = config_.heartbeatInterval;
            agoraConfig.reconnectTimeout = 15; // Set a reasonable reconnect timeout
            agoraConfig.areaCode = RTM_AREA_CODE_GLOB;
            agoraConfig.protocolType = RTM_PROTOCOL_TYPE_TCP_UDP;

            agoraConfig.logConfig.fileSizeInKB = 2 * 1024; // Set default log size to 2 MB
            if (!config_.logFilePath.empty())
            {
                agoraConfig.logConfig.filePath = config_.logFilePath.c_str();
                ELEGOO_LOG_DEBUG("[RTM] Log file path: {}", config_.logFilePath);
            }
            // Create RTM client
            int errorCode = 0;
            rtmClient_ = createAgoraRtmClient(agoraConfig, errorCode);
            if (!rtmClient_ || errorCode != 0)
            {
                rtmClient_ = nullptr; // Ensure nullptr on failure
                throw std::runtime_error("Failed to create Agora RTM client, error code: " + std::to_string(errorCode));
            }

            // Reset state
            isLoggedIn_ = false;
            subscribedChannels_.clear();

            ELEGOO_LOG_DEBUG("[RTM] Client initialized successfully for user: {}", config_.userId);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Failed to initialize RTM client: {}", e.what());
            throw;
        }
    }

    void RtmClient::cleanup()
    {
        // Set shutdown flag to prevent new operations from starting
        isShutdown_.store(true);

        {
            std::unique_lock<std::shared_mutex> lock(stateMutex_);

            if (rtmClient_)
            {
                if (isLoggedIn_)
                {
                    uint64_t requestId = 0;
                    rtmClient_->logout(requestId);
                }
                rtmClient_->release();
                rtmClient_ = nullptr;
            }
            subscribedChannels_.clear();
            isLoggedIn_ = false;
        }
    }

    VoidResult RtmClient::login(const std::string &token)
    {
        // Check shutdown status
        if (isShutdown_.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client is shutting down");
        }

        // Read lock to check status
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex_);
            if (!rtmClient_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            if (isLoggedIn_ && eventHandler_->getCurrentConnectionState() == RTM_LINK_STATE_CONNECTED)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Already logged in");
            }
        }

        try
        {
            uint64_t requestId = 0;
            std::string loginToken;

            {
                std::shared_lock<std::shared_mutex> lock(stateMutex_);
                loginToken = token.empty() ? config_.token : token;
            }

            // Initiate login request - need to hold lock to protect rtmClient_
            IRtmClient *client = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(stateMutex_);
                client = rtmClient_;
            }

            if (!client)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            eventHandler_->resetLoginState();
            eventHandler_->resetConnectionState();

            client->login(loginToken.c_str(), requestId);
            ELEGOO_LOG_DEBUG("[RTM] Login initiated for user: {}, requestId: {}", config_.userId, requestId);

            // Wait for login result
            VoidResult loginResult = eventHandler_->waitForLoginResult(requestId);
            if (loginResult.isSuccess())
            {
                // Login successful, wait for connection state to change to connected
                if (eventHandler_->waitForConnectionState(RTM_LINK_STATE_CONNECTED))
                {
                    {
                        std::unique_lock<std::shared_mutex> lock(stateMutex_);
                        isLoggedIn_ = true;
                    }
                    ELEGOO_LOG_DEBUG("[RTM] Login completed successfully for user: {}", config_.userId);
                    return VoidResult::Success();
                }
                else
                {
                    ELEGOO_LOG_ERROR("[RTM] Login succeeded but connection failed for user: {}", config_.userId);
                    return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "Connection timeout after login");
                }
            }
            else
            {
                ELEGOO_LOG_ERROR("[RTM] Login failed for user: {}, error: {}", config_.userId, loginResult.message);
                return loginResult;
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Login exception: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, e.what());
        }
    }

    bool RtmClient::isOnline() const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return isLoggedIn_ && eventHandler_->getCurrentConnectionState() == RTM_LINK_STATE_CONNECTED;
    }

    VoidResult RtmClient::logout()
    {
        // Read lock to check status
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex_);
            if (!rtmClient_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            if (!isLoggedIn_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Not logged in");
            }
        }

        try
        {
            uint64_t requestId = 0;

            // Need to hold lock to protect rtmClient_
            IRtmClient *client = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(stateMutex_);
                client = rtmClient_;
            }

            if (!client)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            client->logout(requestId);

            {
                std::unique_lock<std::shared_mutex> lock(stateMutex_);
                isLoggedIn_ = false;
                subscribedChannels_.clear();
            }

            ELEGOO_LOG_DEBUG("[RTM] Logout initiated for user: {}, requestId: {}", config_.userId, requestId);
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Logout exception: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, e.what());
        }
    }

    VoidResult RtmClient::subscribe(const std::string &channelName)
    {
        // Clean up expired states before new request
        eventHandler_->cleanupExpiredStates();

        // Check shutdown status
        if (isShutdown_.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client is shutting down");
        }

        if (channelName.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Channel name cannot be empty");
        }

        // Read lock to check status
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex_);
            if (!rtmClient_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            if (!isLoggedIn_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Not logged in");
            }

            if (subscribedChannels_.find(channelName) != subscribedChannels_.end())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Already subscribed to channel: " + channelName);
            }
        }

        try
        {
            SubscribeOptions options;
            options.withMessage = true;
            options.withPresence = true;
            uint64_t requestId = 0;

            // Initiate subscribe request - need to hold lock to protect rtmClient_
            IRtmClient *client = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(stateMutex_);
                client = rtmClient_;
            }

            if (!client)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            // Call API to initiate subscribe, callback will create state
            client->subscribe(channelName.c_str(), options, requestId);
            ELEGOO_LOG_DEBUG("[RTM] Subscribe initiated for channel: {}, requestId: {}", channelName, requestId);

            // Wait for subscribe result
            std::string errorMessage;
            if (eventHandler_->waitForSubscribeResult(requestId, errorMessage))
            {
                {
                    std::unique_lock<std::shared_mutex> lock(stateMutex_);
                    subscribedChannels_.insert(channelName);
                }
                ELEGOO_LOG_DEBUG("[RTM] Subscribe completed successfully for channel: {}", channelName);
                return VoidResult::Success();
            }
            else
            {
                ELEGOO_LOG_ERROR("[RTM] Subscribe failed for channel: {}, error: {}", channelName, errorMessage);
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMessage);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Subscribe exception: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, e.what());
        }
    }

    VoidResult RtmClient::unsubscribe(const std::string &channelName)
    {
        // Clean up expired states before new request
        eventHandler_->cleanupExpiredStates();

        // Check shutdown status
        if (isShutdown_.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client is shutting down");
        }

        if (channelName.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Channel name cannot be empty");
        }

        // Read lock to check status
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex_);
            if (!rtmClient_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            if (!isLoggedIn_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Not logged in");
            }

            if (subscribedChannels_.find(channelName) == subscribedChannels_.end())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Not subscribed to channel: " + channelName);
            }
        }

        try
        {
            uint64_t requestId = 0;

            // Initiate unsubscribe request - need to hold lock to protect rtmClient_
            IRtmClient *client = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(stateMutex_);
                client = rtmClient_;
            }

            if (!client)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            // Call API to initiate unsubscribe, callback will create state
            client->unsubscribe(channelName.c_str(), requestId);
            ELEGOO_LOG_DEBUG("[RTM] Unsubscribe initiated for channel: {}, requestId: {}", channelName, requestId);

            // Wait for unsubscribe result
            std::string errorMessage;
            if (eventHandler_->waitForUnsubscribeResult(requestId, errorMessage))
            {
                {
                    std::unique_lock<std::shared_mutex> lock(stateMutex_);
                    subscribedChannels_.erase(channelName);
                }
                ELEGOO_LOG_DEBUG("[RTM] Unsubscribe completed successfully for channel: {}", channelName);
                return VoidResult::Success();
            }
            else
            {
                ELEGOO_LOG_ERROR("[RTM] Unsubscribe failed for channel: {}, error: {}", channelName, errorMessage);
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMessage);
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Unsubscribe exception: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, e.what());
        }
    }

    VoidResult RtmClient::publish(const std::string &channelName, const std::string &message)
    {
        // Clean up expired states before new request
        eventHandler_->cleanupExpiredStates();

        // Check shutdown status
        if (isShutdown_.load())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client is shutting down");
        }

        if (channelName.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Channel name cannot be empty");
        }

        if (message.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Message cannot be empty");
        }

        // Read lock to check status
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex_);
            if (!rtmClient_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            if (!isLoggedIn_)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "Not logged in");
            }

            if (eventHandler_->getCurrentConnectionState() != RTM_LINK_STATE_CONNECTED)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "RTM client not connected");
            }
        }

        try
        {
            PublishOptions options;
            options.messageType = RTM_MESSAGE_TYPE_STRING;
            options.channelType = RTM_CHANNEL_TYPE_USER;
            options.customType = "PlainText";
            uint64_t requestId = 0;

            // Initiate publish request - need to hold lock to protect rtmClient_
            IRtmClient *client = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(stateMutex_);
                client = rtmClient_;
            }

            if (!client)
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
            }

            // Call API to initiate publish, callback will create state
            client->publish(channelName.c_str(), message.c_str(), message.size(), options, requestId);
            ELEGOO_LOG_DEBUG("[RTM] Publish initiated for channel: {}, message: {}, requestId: {}", StringUtils::maskString(channelName), message, requestId);

            VoidResult publishResult = eventHandler_->waitForPublishResult(requestId);
            if (publishResult.isSuccess())
            {
                ELEGOO_LOG_DEBUG("[RTM] Publish completed successfully for channel: {}", StringUtils::maskString(channelName));
            }
            else
            {
                ELEGOO_LOG_DEBUG("[RTM] Publish failed for channel: {}, error: {}", StringUtils::maskString(channelName), publishResult.message);
            }
            return publishResult;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Publish exception: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, e.what());
        }
    }

    VoidResult RtmClient::renewToken(const std::string &token)
    {
        if (token.empty())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Token cannot be empty");
        }

        // Need to hold lock to protect rtmClient_
        IRtmClient *client = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex_);
            client = rtmClient_;
        }

        if (!client)
        {
            return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "RTM client not initialized");
        }

        try
        {
            uint64_t requestId = 0;
            client->renewToken(token.c_str(), requestId);

            {
                std::unique_lock<std::shared_mutex> lock(stateMutex_);
                config_.token = token;
            }

            ELEGOO_LOG_DEBUG("[RTM] Token renew initiated, requestId: {}", requestId);
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Renew token exception: {}", e.what());
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, e.what());
        }
    }

    VoidResult RtmClient::updateConfig(const RtmConfig &newConfig)
    {
        // Use exclusive lock for configuration update
        std::unique_lock<std::shared_mutex> lock(stateMutex_);

        // If user ID changed, need to reinitialize RTM client
        if (config_.userId != newConfig.userId || config_.appId != newConfig.appId)
        {
            // Logout old user first
            if (isLoggedIn_)
            {
                // Release lock, call logout, then reacquire lock
                lock.unlock();
                logout();
                lock.lock();
            }

            // Temporarily release lock, call cleanup, then reacquire lock
            lock.unlock();
            cleanup();
            lock.lock();

            // Update configuration
            config_ = newConfig;

            // Reinitialize
            try
            {
                // Reset shutdown flag
                isShutdown_.store(false);
                lock.unlock();
                initialize();
                lock.lock();
                ELEGOO_LOG_DEBUG("[RTM] Client reinitialized for new user: {}", config_.userId);
                return VoidResult::Success();
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[RTM] Failed to reinitialize with new config: {}", e.what());
                return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, e.what());
            }
        }
        else
        {
            // Just token change, can update directly
            config_ = newConfig;
            ELEGOO_LOG_DEBUG("[RTM] Configuration updated");
            return VoidResult::Success();
        }
    }

    // Getters
    bool RtmClient::isLoggedIn() const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return isLoggedIn_;
    }

    RtmConnectionState RtmClient::getConnectionState() const
    {
        return eventHandler_->getCurrentConnectionState();
    }

    RtmConnectionChangeReason RtmClient::getConnectionChangeReason() const
    {
        return eventHandler_->getCurrentConnectionChangeReason();
    }

    bool RtmClient::isSubscribed(const std::string &channelName) const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return subscribedChannels_.find(channelName) != subscribedChannels_.end();
    }

    std::vector<std::string> RtmClient::getSubscribedChannels() const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return std::vector<std::string>(subscribedChannels_.begin(), subscribedChannels_.end());
    }

    std::string RtmClient::getUserId() const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return config_.userId;
    }

    std::string RtmClient::getAppId() const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        return config_.appId;
    }

    // Set callbacks
    void RtmClient::setMessageCallback(const RtmMessageCallback &callback)
    {
        eventHandler_->setMessageCallback(callback);
    }
    void RtmClient::setPresenceCallback(const RtmPresenceCallback &callback)
    {
        eventHandler_->setPresenceCallback(callback);
    }
    void RtmClient::setConnectionStateCallback(const RtmConnectionStateCallback &callback)
    {
        eventHandler_->setConnectionStateCallback(callback);
    }

    // ==================== RtmClient Factory Function ====================

    std::unique_ptr<RtmClient> createRtmClient(const RtmConfig &config)
    {
        try
        {
            return std::make_unique<RtmClient>(config);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("[RTM] Failed to create RTM client: {}", e.what());
            return nullptr;
        }
    }

} // namespace elink
