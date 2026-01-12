#include "protocols/mqtt_protocol.h"
#include "protocols/protocol_interface.h"
#include "protocols/connection_manager_base.h"
#include "protocols/error_handler.h"
#include "utils/logger.h"
#include "utils/utils.h"
// Prevent Windows headers from defining max/min macros
#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

// Undefine max/min macros if they are already defined
#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace elink
{
    /**
     * @brief Refactored MQTT protocol implementation
     *
     * Inherits from ConnectionManagerBase and provides virtual methods for customization:
     * - Authentication logic
     * - Topic management
     * - Registration process
     * - Heartbeat mechanism
     * - Message handling
     *
     * Common logic like connection management and reconnection is handled by the base class
     */
    class MqttProtocol::Impl : public ConnectionManagerBase, public mqtt::callback
    {
    public:
        Impl(MqttProtocol *parent) : ConnectionManagerBase("MQTT"),
                                     parent_(parent), client_(nullptr), isRegistering_(false), registrationSuccess_(false),
                                     heartbeatRunning_(false), lastPongReceived_(std::chrono::steady_clock::now()) {}

        ~Impl()
        {
            disconnect();
            stopHeartbeat();
        }

        /**
         * @brief Send command to MQTT broker
         */
        bool sendCommand(const std::string &data)
        {
            if (!isConnected())
            {
                ELEGOO_LOG_ERROR("[{}] MQTT not connected", lastConnectParams_.host);
                return false;
            }

            try
            {
                std::string topic = parent_->getCommandTopic(lastConnectParams_);

                ELEGOO_LOG_DEBUG("[{}] Sending MQTT command: {}", lastConnectParams_.host, data);

                std::lock_guard<std::mutex> lock(clientMutex_);
                if (!client_ || !client_->is_connected())
                {
                    ELEGOO_LOG_ERROR("[{}] MQTT client unavailable during send", lastConnectParams_.host);
                    return false;
                }
                auto token = client_->publish(topic, data, 1, false);
                return token->wait_for(std::chrono::seconds(2));
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[{}] MQTT send command failed: {}", lastConnectParams_.host, e.what());
                return false;
            }
        }

        /**
         * @brief Set message receive callback function
         */
        void setMessageCallback(std::function<void(const std::string &)> callback)
        {
            std::lock_guard<std::mutex> lock(messageCallbackMutex_);
            messageCallback_ = callback;
        }

        /**
         * @brief Get MQTT client for subclasses
         */
        mqtt::async_client *getMqttClient() const
        {
            std::lock_guard<std::mutex> lock(clientMutex_);
            return client_.get();
        }

    protected:
        /**
         * @brief Implement specific MQTT connection logic
         */
        VoidResult doConnect(const ConnectPrinterParams &connectParams) override
        {
            auto startTime = std::chrono::steady_clock::now();

            // 1. Validate connection parameters

            auto validationResult = parent_->validateConnectionParams(connectParams);
            if (!validationResult.isSuccess())
            {
                return ErrorHandler::createConnectionFailure(
                    validationResult.code,
                    validationResult.message,
                    "",
                    startTime);
            }

            // 2. Build MQTT server address and client ID
            std::string serverUri = parent_->processConnectionUrl(connectParams);
            if (serverUri.empty())
            {
                return ErrorHandler::createConnectionFailure(
                    ELINK_ERROR_CODE::INVALID_PARAMETER,
                    "Invalid server URI",
                    "Failed to process connection URL",
                    startTime);
            }
            std::string clientId = parent_->getClientId(connectParams);

            ELEGOO_LOG_DEBUG("[{}] MQTT connecting to {} with client ID: {}", lastConnectParams_.host, serverUri, clientId);

            // 3. Create MQTT client
            std::unique_ptr<mqtt::async_client> newClient;
            try
            {
                newClient = std::make_unique<mqtt::async_client>(serverUri, clientId);
                newClient->set_callback(*this);
            }
            catch (const mqtt::exception &e)
            {
                return ErrorHandler::createConnectionFailure(
                    ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR,
                    "Failed to create MQTT client",
                    "MQTT client creation error: " + std::string(e.what()) +
                        " (Server: " + serverUri + ", Client ID: " + clientId + ")",
                    startTime);
            }

            // 3. Set connection options using virtual method
            mqtt::connect_options conn_opts;
            conn_opts.set_keep_alive_interval(60);
            conn_opts.set_clean_session(true);
            conn_opts.set_automatic_reconnect(false); // We manage reconnection ourselves

            parent_->configureConnectionOptions(conn_opts, connectParams);

            // 4. Attempt connection
            ELEGOO_LOG_DEBUG("[{}] MQTT attempting connection", lastConnectParams_.host);

            int connectionTimeout = 5000; // Default connection timeout is 5 seconds
            if (connectParams.connectionTimeout > 0)
            {
                connectionTimeout = connectParams.connectionTimeout;
            }

            try
            {
                auto token = newClient->connect(conn_opts);
                if (token == nullptr)
                {
                    return ErrorHandler::createConnectionFailure(
                        ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR,
                        "Failed to create MQTT connection",
                        "",
                        startTime);
                }

                bool connectionSuccess = token->wait_for(std::chrono::milliseconds(connectionTimeout));
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime);

                if (!connectionSuccess)
                {
                    int reasonCode = token->get_return_code();
                    std::string reasonMessage = token->get_error_message();
                    ELEGOO_LOG_ERROR("[{}] MQTT connection failed: {} (reason code: {})", lastConnectParams_.host, reasonMessage, reasonCode);
                    return ErrorHandler::createTimeoutFailure("MQTT", startTime);
                }

                // 5. Verify connection status
                if (!newClient->is_connected())
                {
                    return ErrorHandler::createConnectionFailure(
                        ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR,
                        "MQTT connection failed",
                        "Connection attempt completed but client reports not connected. "
                        "This may indicate authentication failure or broker rejection.",
                        startTime);
                }

                // Connection successful, update the client under lock
                {
                    std::lock_guard<std::mutex> lock(clientMutex_);
                    client_ = std::move(newClient);
                }

                // 6. Subscribe to topics using virtual method
                try
                {
                    auto subscriptionTopics = parent_->getSubscriptionTopics(connectParams);
                    for (const auto &topic : subscriptionTopics)
                    {
                        std::lock_guard<std::mutex> lock(clientMutex_);
                        if (client_)
                        {
                            client_->subscribe(topic, 1);
                            ELEGOO_LOG_DEBUG("[{}] Subscribed to topic: {}", lastConnectParams_.host, StringUtils::maskString(topic));
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_WARN("[{}] Topic subscription warning: {}", lastConnectParams_.host, e.what());
                    // Subscription failure doesn't affect connection success
                }

                // 7. Execute printer registration process (if required)
                if (parent_->requiresRegistration())
                {
                    ELEGOO_LOG_INFO("[{}] Printer requires registration, starting registration process...", lastConnectParams_.host);

                    // Set registration status
                    isRegistering_ = true;
                    registrationSuccess_ = false;

                    // Perform registration using virtual method
                    std::string registrationClientId = parent_->getClientId(connectParams);
                    bool registrationSent = parent_->performRegistration(
                        connectParams,
                        registrationClientId,
                        [this](const std::string &topic, const std::string &message) -> bool
                        {
                            try
                            {
                                std::lock_guard<std::mutex> lock(clientMutex_);
                                if (!client_ || !client_->is_connected())
                                {
                                    return false;
                                }
                                auto token = client_->publish(topic, message, 1, false);
                                return token->wait_for(std::chrono::seconds(2));
                            }
                            catch (const std::exception &e)
                            {
                                ELEGOO_LOG_ERROR("Failed to send registration message: {}", e.what());
                                return false;
                            }
                        });

                    if (!registrationSent)
                    {
                        isRegistering_ = false;
                        return ErrorHandler::createConnectionFailure(
                            ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR,
                            "Printer registration failed",
                            "Failed to send registration request",
                            startTime);
                    }

                    int registrationTimeoutMs = parent_->getRegistrationTimeoutMs();
                    if (connectParams.connectionTimeout > 0)
                    {
                        registrationTimeoutMs = connectParams.connectionTimeout;
                    }

                    // Wait for registration response
                    auto registrationTimeout = std::chrono::milliseconds(registrationTimeoutMs);
                    auto registrationEndTime = std::chrono::steady_clock::now() + registrationTimeout;

                    std::unique_lock<std::mutex> lock(registrationMutex_);
                    while (isRegistering_ && std::chrono::steady_clock::now() < registrationEndTime)
                    {
                        if (registrationCondition_.wait_for(lock, std::chrono::milliseconds(100)) == std::cv_status::no_timeout)
                        {
                            if (!isRegistering_)
                            {
                                break; // Registration completed
                            }
                        }
                    }

                    if (isRegistering_)
                    {
                        // Registration timeout
                        isRegistering_ = false;
                        registrationError_ = ELINK_ERROR_CODE::OPERATION_TIMEOUT;
                        registrationErrorMessage_ = "Printer registration timed out";
                    }

                    if (!registrationSuccess_)
                    {
                        try
                        {
                            std::lock_guard<std::mutex> lock(clientMutex_);
                            if (client_ && client_->is_connected())
                            {
                                auto token = client_->disconnect();
                                token->wait_for(std::chrono::seconds(2));
                            }
                        }
                        catch (const mqtt::exception &e)
                        {
                            ELEGOO_LOG_ERROR("[{}] MQTT disconnect error: {}", lastConnectParams_.host, e.what());
                        }
                        std::string errorMessage = registrationErrorMessage_.empty() ? "Unknown registration error" : registrationErrorMessage_;
                        ELEGOO_LOG_ERROR("[{}] Printer registration failed: {}", lastConnectParams_.host, errorMessage);
                        return ErrorHandler::createConnectionFailure(
                            registrationError_,
                            errorMessage,
                            "",
                            startTime);
                    }

                    ELEGOO_LOG_INFO("[{}] Printer registration completed successfully", lastConnectParams_.host);
                }

                ELEGOO_LOG_INFO("[{}] MQTT connected successfully to {} (duration: {}ms)",
                                lastConnectParams_.host, serverUri, duration.count());

                // Start heartbeat if enabled
                if (parent_->isHeartbeatEnabled())
                {
                    startHeartbeat();
                }

                return VoidResult::Success();
            }
            catch (const mqtt::exception &e)
            {
                int returnCode = e.get_return_code();
                std::string errorMessage = e.what();
                ELINK_ERROR_CODE errorCode = ErrorHandler::mapMqttReturnCode(returnCode, connectParams.authMode);
                return VoidResult::Error(errorCode, errorMessage);
            }
        }

        /**
         * @brief Implement specific MQTT disconnection logic
         */
        void doDisconnect() override
        {
            stopHeartbeat();

            std::lock_guard<std::mutex> lock(clientMutex_);
            if (client_)
            {
                try
                {
                    if (client_->is_connected())
                    {
                        auto token = client_->disconnect();
                        token->wait_for(std::chrono::seconds(2));
                    }
                }
                catch (const mqtt::exception &e)
                {
                    ELEGOO_LOG_ERROR("[{}] MQTT disconnect error: {}", lastConnectParams_.host, e.what());
                }
                client_.reset(); // Clear the client pointer
            }
        }

        /**
         * @brief Check underlying MQTT connection status
         */
        bool isUnderlyingConnected() const override
        {
            std::lock_guard<std::mutex> lock(clientMutex_);
            return client_ && client_->is_connected();
        }

        // ============ MQTT callback interface implementation ============

        /**
         * @brief MQTT connection lost callback
         */
        void connection_lost(const std::string &cause) override
        {
            ELEGOO_LOG_ERROR("[{}] MQTT connection lost: {}", lastConnectParams_.host, cause);
            startAutoReconnect(); // Use base class reconnection mechanism
        }

        /**
         * @brief MQTT message arrived callback
         */
        void message_arrived(mqtt::const_message_ptr msg) override
        {
            try
            {
                std::string topic = msg->get_topic();
                std::string payload = msg->to_string();
                ELEGOO_LOG_DEBUG("[{}] MQTT message arrived from topic {}: {}", lastConnectParams_.host, StringUtils::maskString(topic), payload);

                // Check if this is a registration response
                if (isRegistering_)
                {
                    if (parent_->isRegistrationMessage(topic, payload))
                    {
                        std::string errorMessage;
                        ELINK_ERROR_CODE errorCode = ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR;
                        std::string clientId = parent_->getClientId(lastConnectParams_);
                        if (parent_->validateRegistrationResponse(topic, payload, clientId, errorCode, errorMessage))
                        {
                            std::lock_guard<std::mutex> lock(registrationMutex_);
                            registrationSuccess_ = true;
                            isRegistering_ = false;
                            registrationError_ = ELINK_ERROR_CODE::SUCCESS;
                            registrationErrorMessage_ = "";
                            registrationCondition_.notify_one();
                            ELEGOO_LOG_INFO("[{}] Printer registration successful", lastConnectParams_.host);
                            return; // Don't pass registration messages to business layer
                        }
                        else
                        {
                            std::lock_guard<std::mutex> lock(registrationMutex_);
                            registrationSuccess_ = false;
                            isRegistering_ = false;
                            registrationErrorMessage_ = errorMessage;
                            registrationError_ = errorCode;
                            registrationCondition_.notify_one();
                            ELEGOO_LOG_WARN("[{}] Printer registration failed: {}", lastConnectParams_.host, errorMessage);
                            return; // Don't pass registration messages to business layer
                        }
                    }
                }

                // Check if this is a heartbeat response
                if (parent_->isHeartbeatEnabled() && parent_->handleHeartbeatResponse(payload))
                {
                    std::lock_guard<std::mutex> lock(heartbeatMutex_);
                    lastPongReceived_ = std::chrono::steady_clock::now();
                    ELEGOO_LOG_DEBUG("[{}] MQTT heartbeat response received", lastConnectParams_.host);
                    return; // Don't pass heartbeat messages to business layer
                }

                // Handle message using virtual method
                parent_->handleMessage(topic, payload);

                // Also call the callback function for backward compatibility
                std::function<void(const std::string &)> callback;
                {
                    std::lock_guard<std::mutex> lock(messageCallbackMutex_);
                    callback = messageCallback_;
                }

                // Call callback function outside of lock to avoid deadlock
                if (callback)
                {
                    callback(payload);
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[{}] Error processing MQTT message: {}", lastConnectParams_.host, e.what());
            }
        }

        /**
         * @brief MQTT message delivery complete callback
         */
        void delivery_complete(mqtt::delivery_token_ptr token) override
        {
            ELEGOO_LOG_DEBUG("[{}] MQTT message delivery completed", lastConnectParams_.host);
        }

    private:
        /**
         * @brief Start heartbeat thread
         */
        void startHeartbeat()
        {
            stopHeartbeat();
            std::lock_guard<std::mutex> lock(heartbeatMutex_);
            if (!heartbeatRunning_)
            {
                heartbeatRunning_ = true;
                lastPongReceived_ = std::chrono::steady_clock::now();
                heartbeatThread_ = std::thread(&Impl::heartbeatLoop, this);
                ELEGOO_LOG_DEBUG("[{}] MQTT heartbeat started", lastConnectParams_.host);
            }
        }

        /**
         * @brief Stop heartbeat thread
         */
        void stopHeartbeat()
        {
            {
                std::lock_guard<std::mutex> lock(heartbeatMutex_);
                heartbeatRunning_ = false;
            }

            if (heartbeatThread_.joinable())
            {
                heartbeatThread_.join();
                ELEGOO_LOG_DEBUG("[{}] MQTT heartbeat stopped", lastConnectParams_.host);
            }
        }

        /**
         * @brief Heartbeat loop
         */
        void heartbeatLoop()
        {
            while (heartbeatRunning_)
            {
                // Wait for the configured interval
                int intervalSeconds = parent_->getHeartbeatIntervalSeconds();
                for (int i = 0; i < intervalSeconds * 10 && heartbeatRunning_; ++i) // intervalSeconds * 10 * 100ms = intervalSeconds seconds
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                if (!heartbeatRunning_)
                {
                    break;
                }

                // Check connection status
                if (!isConnected())
                {
                    ELEGOO_LOG_WARN("[{}] MQTT heartbeat: connection lost, stopping heartbeat", lastConnectParams_.host);
                    break;
                }

                // Send heartbeat using virtual method
                if (!sendHeartbeat())
                {
                    ELEGOO_LOG_ERROR("[{}] MQTT heartbeat: failed to send heartbeat", lastConnectParams_.host);
                    continue;
                }

                // Check heartbeat response timeout
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastResponse = std::chrono::duration_cast<std::chrono::seconds>(
                    now - lastPongReceived_);

                int timeoutSeconds = parent_->getHeartbeatTimeoutSeconds();
                if (timeSinceLastResponse > std::chrono::seconds(timeoutSeconds))
                {
                    ELEGOO_LOG_ERROR("[{}] MQTT heartbeat: response timeout, last response {} seconds ago",
                                     lastConnectParams_.host, timeSinceLastResponse.count());
                    // {
                    //     std::lock_guard<std::mutex> lock(clientMutex_);
                    //     if (client_)
                    //     {
                    //         try
                    //         {
                    //             if (client_->is_connected())
                    //             {
                    //                 ELEGOO_LOG_DEBUG("[{}] MQTT disconnecting due to heartbeat timeout", lastConnectParams_.host);
                    //                 auto token = client_->disconnect();
                    //                 token->wait_for(std::chrono::seconds(2));
                    //             }
                    //         }
                    //         catch (const mqtt::exception &e)
                    //         {
                    //             ELEGOO_LOG_ERROR("[{}] MQTT disconnect error: {}", lastConnectParams_.host, e.what());
                    //         }
                    //     }
                    // }
                    // Trigger reconnection
                    startAutoReconnect();
                    break;
                }
            }
            heartbeatRunning_ = false;
        }

        /**
         * @brief Send heartbeat message using virtual method
         */
        bool sendHeartbeat()
        {
            try
            {
                std::lock_guard<std::mutex> lock(clientMutex_);
                if (!client_ || !client_->is_connected())
                {
                    return false;
                }

                std::string heartbeatMessage = parent_->createHeartbeatMessage();
                std::string heartbeatTopic = parent_->getHeartbeatTopic(lastConnectParams_);

                ELEGOO_LOG_DEBUG("[{}] Sending MQTT heartbeat: {}", lastConnectParams_.host, heartbeatMessage);

                if (heartbeatTopic.empty())
                {
                    ELEGOO_LOG_ERROR("[{}] MQTT heartbeat: no topic available for heartbeat", lastConnectParams_.host);
                    return false;
                }

                auto token = client_->publish(heartbeatTopic, heartbeatMessage, 1, false);
                return token->wait_for(std::chrono::seconds(2));
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[{}] MQTT send heartbeat failed: {}", lastConnectParams_.host, e.what());
                return false;
            }
        }

    private:
        // ============ MQTT client related ============
        MqttProtocol *parent_;
        std::unique_ptr<mqtt::async_client> client_;
        mutable std::mutex clientMutex_; // Protect client_ access

        // ============ Callback functions ============
        std::function<void(const std::string &)> messageCallback_;
        std::mutex messageCallbackMutex_;

        // ============ Printer registration related ============
        std::atomic<bool> isRegistering_;
        std::atomic<bool> registrationSuccess_;
        std::string registrationErrorMessage_;
        ELINK_ERROR_CODE registrationError_{ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR};
        std::mutex registrationMutex_;
        std::condition_variable registrationCondition_;

        // ============ Heartbeat related ============
        std::atomic<bool> heartbeatRunning_;
        std::thread heartbeatThread_;
        std::chrono::steady_clock::time_point lastPongReceived_;
        mutable std::mutex heartbeatMutex_;
    };

    // ============ MqttProtocol public interface implementation ============

    MqttProtocol::MqttProtocol() : impl_(std::make_unique<Impl>(this))
    {
    }

    MqttProtocol::~MqttProtocol() = default;

    VoidResult MqttProtocol::connect(const ConnectPrinterParams &connectParams, bool autoReconnect)
    {
        return impl_->connect(connectParams, autoReconnect);
    }

    void MqttProtocol::disconnect()
    {
        impl_->disconnect();
    }

    bool MqttProtocol::isConnected() const
    {
        return impl_->isConnected();
    }

    bool MqttProtocol::sendCommand(const std::string &data)
    {
        return impl_->sendCommand(data);
    }

    void MqttProtocol::setMessageCallback(std::function<void(const std::string &)> callback)
    {
        impl_->setMessageCallback(callback);
    }

    void MqttProtocol::setConnectStatusCallback(std::function<void(bool)> callback)
    {
        impl_->setStatusCallback(callback);
    }

    // ============ Protected virtual methods with default implementations ============
    std::string MqttProtocol::getClientId(const ConnectPrinterParams &connectParams) const
    {
        return "";
    }

    VoidResult MqttProtocol::validateConnectionParams(const ConnectPrinterParams &connectParams) const
    {
        // Check if connection URL is valid
        if (connectParams.host.empty())
        {
            return BizResult<>::Error(ELINK_ERROR_CODE::INVALID_PARAMETER, "Host is required");
        }
        return BizResult<>::Success();
    }

    void MqttProtocol::configureConnectionOptions(mqtt::connect_options &conn_opts,
                                                  const ConnectPrinterParams &connectParams)
    {
        // Default authentication logic
        if (connectParams.authMode == "basic")
        {
            if (!connectParams.username.empty())
            {
                conn_opts.set_user_name(connectParams.username);
            }

            if (!connectParams.password.empty())
            {
                conn_opts.set_password(connectParams.password);
            }
        }
        else if (connectParams.authMode == "token")
        {
            if (!connectParams.token.empty())
            {
                conn_opts.set_password(connectParams.token);
            }
        }
        else
        {
        }
    }

    bool MqttProtocol::requiresRegistration() const
    {
        // Default: no registration required
        return false;
    }

    bool MqttProtocol::performRegistration(
        const ConnectPrinterParams &connectParams,
        const std::string &clientId,
        std::function<bool(const std::string &, const std::string &)> sendMessageCallback)
    {
        // Default implementation: no registration required
        return true;
    }

    bool MqttProtocol::isRegistrationMessage(const std::string &topic, const std::string &message) const
    {
        return false;
    }

    bool MqttProtocol::validateRegistrationResponse(const std::string &topic,
                                                    const std::string &message,
                                                    const std::string &clientId,
                                                    ELINK_ERROR_CODE &errorCode,
                                                    std::string &errorMessage)
    {
        // Default implementation: no validation required
        return true;
    }

    int MqttProtocol::getRegistrationTimeoutMs() const
    {
        return 2000; // 2 seconds default
    }

    void MqttProtocol::handleMessage(const std::string &topic, const std::string &payload)
    {
        // Default implementation: do nothing, subclasses can override for custom message handling
    }

    bool MqttProtocol::isHeartbeatEnabled() const
    {
        // Default: heartbeat disabled
        return false;
    }

    int MqttProtocol::getHeartbeatIntervalSeconds() const
    {
        return 30; // 30 seconds default
    }

    std::string MqttProtocol::createHeartbeatMessage() const
    {
        return "ping"; // Default heartbeat message
    }

    bool MqttProtocol::handleHeartbeatResponse(const std::string &payload)
    {
        // Default implementation: check for "pong" response
        return payload == "pong";
    }

    std::string MqttProtocol::getHeartbeatTopic(const ConnectPrinterParams &connectParams) const
    {
        // Default: use command topic for heartbeat
        return getCommandTopic(connectParams);
    }

    int MqttProtocol::getHeartbeatTimeoutSeconds() const
    {
        return 65; // 65 seconds default timeout
    }
    mqtt::async_client *MqttProtocol::getMqttClient() const
    {
        return impl_->getMqttClient();
    }

} // namespace elink
