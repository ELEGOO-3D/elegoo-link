#pragma once

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <optional>
#include <thread>
#include <atomic>
#include "types/biz.h"
#include <nlohmann/json.hpp>
#include "IAgoraRtmClient.h"
// Forward declaration of Agora RTM related types
namespace agora
{
    namespace rtm
    {
        class IRtmClient;
        class IRtmEventHandler;
        // enum RTM_ERROR_CODE;
        // enum RTM_CONNECTION_STATE;
        // enum RTM_CONNECTION_CHANGE_REASON;
        struct MessageEvent;
        struct LinkStateEvent;
        struct RtmConfig;
        struct PublishOptions;
        struct SubscribeOptions;
    }
}

namespace elink
{
    /**
     * RTM message structure
     */
    struct RtmMessage
    {
        std::string channelName; // Channel name
        std::string publisher;   // Publisher ID
        std::string content;     // Message content
        int64_t timestamp;       // Timestamp
    };

    /**
     * RTM connection configuration
     */
    struct RtmConfig
    {
        std::string appId;  // Agora application ID
        std::string userId; // User ID
        std::string token;  // Access token (optional)

        // Connection configuration
        int presenceTimeout = 300;  // Presence timeout (seconds)
        int heartbeatInterval = 30; // Heartbeat interval (seconds)
    };

    // Directly use RTM SDK's enumeration types
    using RtmConnectionState = agora::rtm::RTM_CONNECTION_STATE;
    using RtmConnectionChangeReason = agora::rtm::RTM_CONNECTION_CHANGE_REASON;

    /**
     * RTM callback function type definitions
     */
    using RtmMessageCallback = std::function<void(const RtmMessage &message)>;
    using RtmPresenceCallback = std::function<void(const std::string &channelName, const std::string &userId, bool online)>;
    using RtmConnectionStateCallback = std::function<void(RtmConnectionState state, RtmConnectionChangeReason reason)>;

    class RtmClient
    {
    public:
        /**
         * Constructor
         * @param config RTM connection configuration
         */
        explicit RtmClient(const RtmConfig &config);

        /**
         * Destructor
         */
        ~RtmClient();

        // Disallow copy construction and assignment
        RtmClient(const RtmClient &) = delete;
        RtmClient &operator=(const RtmClient &) = delete;

        // ==================== Connection Management ====================

        /**
         * Login to RTM service
         * @param token Access token (optional)
         * @return Login result
         */
        VoidResult login(const std::string &token = "");

        /**
         * Logout from RTM service
         * @return Logout result
         */
        VoidResult logout();

        /**
         * Check if logged in
         * @return true if logged in
         */
        bool isLoggedIn() const;

        /**
         * Check if online (logged in and connected)
         * @return true if online
         */
        bool isOnline() const;

        /**
         * Get current connection state
         * @return Connection state
         */
        RtmConnectionState getConnectionState() const;

        /**
         * Get connection state change reason
         * @return Connection change reason
         */
        RtmConnectionChangeReason getConnectionChangeReason() const;

        // ==================== Channel Management ====================

        /**
         * Subscribe to channel
         * @param channelName Channel name
         * @return Subscription result
         */
        VoidResult subscribe(const std::string &channelName);

        /**
         * Unsubscribe from channel
         * @param channelName Channel name
         * @return Unsubscription result
         */
        VoidResult unsubscribe(const std::string &channelName);

        /**
         * Check if subscribed to channel
         * @param channelName Channel name
         * @return true if subscribed
         */
        bool isSubscribed(const std::string &channelName) const;

        /**
         * Get list of subscribed channels
         * @return List of channel names
         */
        std::vector<std::string> getSubscribedChannels() const;

        // ==================== Message Publishing ====================

        /**
         * Publish message to channel
         * @param channelName Channel name
         * @param message Message content
         * @return Publishing result
         */
        VoidResult publish(const std::string &channelName, const std::string &message);

        /**
         * Publish JSON message to channel
         * @param channelName Channel name
         * @param jsonMessage JSON message
         * @return Publishing result
         */
        VoidResult publishJson(const std::string &channelName, const nlohmann::json &jsonMessage);

        // ==================== Callback Management ====================

        /**
         * Set message callback
         * @param callback Message callback function
         */
        void setMessageCallback(const RtmMessageCallback &callback);

        /**
         * Set presence callback
         * @param callback Presence callback function
         */
        void setPresenceCallback(const RtmPresenceCallback &callback);

        /**
         * Set connection state callback
         * @param callback Connection state callback function, parameters are connection state and reason
         */
        void setConnectionStateCallback(const RtmConnectionStateCallback &callback);

        // ==================== Utility Methods ====================

        /**
         * Get user ID
         * @return User ID
         */
        std::string getUserId() const;

        /**
         * Get application ID
         * @return Application ID
         */
        std::string getAppId() const;

        /**
         * Update token
         * @param token New token
         * @return Update result
         */
        VoidResult renewToken(const std::string &token);

        /**
         * Update RTM configuration (supports user switching)
         * @param config New RTM configuration
         * @return Update result
         */
        VoidResult updateConfig(const RtmConfig &config);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /**
     * RTM client factory function
     * @param config RTM configuration
     * @return RTM client smart pointer
     */
    std::unique_ptr<RtmClient> createRtmClient(const RtmConfig &config);

} // namespace elink
