#include "protocols/mqtt_client.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <mqtt/async_client.h>
#include <mqtt/client.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#include <mqtt/topic.h>
#include <mqtt/callback.h>
#include <mqtt/properties.h>
#include <mqtt/types.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <variant>
#include <private_config.h>
#include <utils/utils.h>
using namespace std::chrono_literals;
// wss://
#define DEFAULT_MQTT_WS_PREFIX "ws://"
namespace elink
{

    // ==================== MQTT Callback Handler ====================

    class MqttCallbackHandler : public virtual mqtt::callback, public virtual mqtt::iaction_listener
    {
    public:
        explicit MqttCallbackHandler(MqttClient::Impl *impl) : m_impl(impl) {}

        void connected(const std::string &cause) override;
        void connection_lost(const std::string &cause) override;
        void message_arrived(mqtt::const_message_ptr msg) override;
        void delivery_complete(mqtt::delivery_token_ptr token) override;

        // Action listener methods
        void on_failure(const mqtt::token &tok) override;
        void on_success(const mqtt::token &tok) override;

    private:
        MqttClient::Impl *m_impl;
    };

    // ==================== MqttClient::Impl Implementation ====================

    class MqttClient::Impl
    {
    public:
        explicit Impl(const MqttConfig &config);
        ~Impl();

        // Connection management
        VoidResult connect();
        VoidResult disconnect();
        bool isConnected() const;
        MqttConnectionState getConnectionState() const;

        // Message publishing
        VoidResult publish(const std::string &topic, const std::string &payload, int qos, bool retained);
        VoidResult publish(const MqttMessage &message);
        VoidResult publishJson(const std::string &topic, const nlohmann::json &jsonPayload, int qos, bool retained);
        void publishAsync(const std::string &topic, const std::string &payload, int qos, bool retained, MqttPublishCallback callback);

        // Message subscription
        VoidResult subscribe(const std::string &topic, int qos);
        VoidResult subscribe(const std::map<std::string, int> &topics);
        VoidResult unsubscribe(const std::string &topic);
        VoidResult unsubscribe(const std::vector<std::string> &topics);

        // Callback settings
        void setMessageCallback(MqttMessageCallback callback);
        void setConnectionCallback(MqttConnectionCallback callback);

        // Configuration management
        VoidResult updateConfig(const MqttConfig &config);
        const MqttConfig &getConfig() const;

        // Internal methods
        void updateConnectionState(MqttConnectionState newState, const std::string &message = "");
        void setupConnectOptions();
        void notifyMessage(const std::string &topic, const MqttMessage &message);

    public:
        MqttConfig m_config;
        std::unique_ptr<mqtt::async_client> m_client;
        std::unique_ptr<MqttCallbackHandler> m_callbackHandler;
        std::unique_ptr<mqtt::connect_options> m_connectOptions;

        // State management
        mutable std::mutex m_stateMutex;
        std::atomic<MqttConnectionState> m_connectionState{MqttConnectionState::DISCONNECTED};

        // Client operation protection (protects all m_client operations)
        mutable std::mutex m_clientMutex;

        // Callback functions
        mutable std::mutex m_callbackMutex;
        MqttMessageCallback m_messageCallback;
        MqttConnectionCallback m_connectionCallback;

        // Statistics (using atomic operations, no mutex needed)
        std::chrono::steady_clock::time_point m_connectTime;
        std::chrono::steady_clock::time_point m_lastMessageTime;

        // Async publish callback mapping
        mutable std::mutex m_publishCallbackMutex;
        std::map<int, MqttPublishCallback> m_publishCallbacks;
    };

    // ==================== MqttCallbackHandler Implementation ====================

    void MqttCallbackHandler::connected(const std::string &cause)
    {
        ELEGOO_LOG_INFO("MQTT connection successful: {}", cause);
        if (m_impl)
        {
            m_impl->updateConnectionState(MqttConnectionState::CONNECTED, cause);
        }
    }

    void MqttCallbackHandler::connection_lost(const std::string &cause)
    {
        ELEGOO_LOG_WARN("MQTT connection lost: {}", cause);
        if (m_impl)
        {
            m_impl->updateConnectionState(MqttConnectionState::CONNECTION_LOST, cause);
        }
    }

    void MqttCallbackHandler::message_arrived(mqtt::const_message_ptr msg)
    {
        if (!m_impl)
            return;

        try
        {
            MqttMessage message;
            message.topic = msg->get_topic();
            message.payload = msg->to_string();
            message.qos = msg->get_qos();
            message.retained = msg->is_retained();

            m_impl->m_lastMessageTime = std::chrono::steady_clock::now();

            ELEGOO_LOG_DEBUG("Received MQTT message: topic={}, payload_size={}, qos={}, payload={}",
                             StringUtils::maskString(message.topic), message.payload.size(), message.qos,
                             message.payload);

            m_impl->notifyMessage(message.topic, message);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while processing MQTT message: {}", e.what());
        }
    }

    void MqttCallbackHandler::delivery_complete(mqtt::delivery_token_ptr token)
    {
        if (!m_impl)
            return;

        try
        {
            int msgId = token->get_message_id();
            MqttPublishCallback callback;

            // Get and remove callback, reduce lock holding time
            {
                std::lock_guard<std::mutex> lock(m_impl->m_publishCallbackMutex);
                auto it = m_impl->m_publishCallbacks.find(msgId);
                if (it != m_impl->m_publishCallbacks.end())
                {
                    callback = it->second;
                    m_impl->m_publishCallbacks.erase(it);
                }
            }

            // Call user callback outside lock to avoid deadlock
            if (callback)
            {
                try
                {
                    callback(true, "", "");
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_ERROR("Error in user publish callback: {}", e.what());
                }
            }

            ELEGOO_LOG_DEBUG("MQTT message delivery completed: msgId={}", msgId);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while handling MQTT delivery completion callback: {}", e.what());
        }
    }

    void MqttCallbackHandler::on_failure(const mqtt::token &tok)
    {
        if (!m_impl)
            return;

        std::string reason = "Unknown error";
        ELEGOO_LOG_ERROR("MQTT operation failed: {}", reason);

        try
        {
            int msgId = tok.get_message_id();
            std::string topic;
            MqttPublishCallback callback;

            // Get and remove callback to reduce lock holding time
            {
                std::lock_guard<std::mutex> lock(m_impl->m_publishCallbackMutex);
                auto it = m_impl->m_publishCallbacks.find(msgId);
                if (it != m_impl->m_publishCallbacks.end())
                {
                    callback = it->second;
                    m_impl->m_publishCallbacks.erase(it);
                }
            }

            // Call user callback outside lock to avoid deadlock
            if (callback)
            {
                try
                {
                    callback(false, topic, reason);
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_ERROR("Error in user publish failure callback: {}", e.what());
                }
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while handling MQTT operation failure callback: {}", e.what());
        }
    }

    void MqttCallbackHandler::on_success(const mqtt::token &tok)
    {
        if (!m_impl)
            return;

        try
        {
            int msgId = tok.get_message_id();
            ELEGOO_LOG_DEBUG("MQTT operation successful: msgId={}", msgId);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred while handling MQTT operation success callback: {}", e.what());
        }
    }

    // ==================== Helper Functions ====================

    // Helper function to ensure broker URL has a proper protocol prefix
    static std::string ensureProtocolPrefix(const std::string &brokerUrl)
    {
        // Check if brokerUrl already has a protocol prefix
        if (brokerUrl.find("ws://") == 0 || brokerUrl.find("wss://") == 0)
        {
            // For WebSocket protocols, check if path is included
            size_t protocolEnd = brokerUrl.find("://") + 3;
            size_t pathStart = brokerUrl.find('/', protocolEnd);

            // If no path or only root path, append /mqtt
            if (pathStart == std::string::npos)
            {
                return brokerUrl + "/mqtt";
            }
            return brokerUrl;
        }

        if (brokerUrl.find("mqtt://") == 0 || brokerUrl.find("mqtts://") == 0)
        {
            // TCP MQTT doesn't need path
            return brokerUrl;
        }

        // Add default ws:// prefix and /mqtt path if no protocol is specified
        return DEFAULT_MQTT_WS_PREFIX + brokerUrl + "/mqtt";
    }

    // Helper function to check if the connection requires SSL/TLS
    static bool requiresSSL(const std::string &serverURI)
    {
        return serverURI.find("wss://") == 0 || serverURI.find("mqtts://") == 0;
    }

    // ==================== MqttClient::Impl  ====================

    MqttClient::Impl::Impl(const MqttConfig &config) : m_config(config)
    {

        try
        {
            m_callbackHandler = std::make_unique<MqttCallbackHandler>(this);
            // Skip initialization if brokerUrl is empty
            if (m_config.brokerUrl.empty())
            {
                ELEGOO_LOG_WARN("MQTT client not initialized: brokerUrl is empty");
                return;
            }
            std::string serverURI = ensureProtocolPrefix(m_config.brokerUrl);
            // auto createOptions = mqtt::create_options_builder()
            //                          .server_uri(serverURI)
            //                          .client_id(m_config.clientId)
            //                          .mqtt_version(MQTTVERSION_5)
            //                          .finalize();
            m_client = std::make_unique<mqtt::async_client>(serverURI, m_config.clientId);

            m_client->set_callback(*m_callbackHandler);
            m_client->set_disconnected_handler([](const mqtt::properties &props, mqtt::ReasonCode code)
                                               { ELEGOO_LOG_INFO("disconnected by broker, reason: {}", mqtt::to_string(code)); });

            setupConnectOptions();

            ELEGOO_LOG_INFO("MQTT client initialized successfully: clientId={}, server={}", m_config.clientId, serverURI);
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("MQTT client initialization failed: {}", e.what());
            throw;
        }
    }

    MqttClient::Impl::~Impl()
    {
        try
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            if (m_client && m_client->is_connected())
            {
                m_client->disconnect()->wait();
            }
            ELEGOO_LOG_INFO("MQTT client destroyed");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error occurred during MQTT client destruction: {}", e.what());
        }
    }

    VoidResult MqttClient::Impl::connect()
    {
        try
        {
            mqtt::token_ptr connectToken;
            bool alreadyConnected = false;
            std::string serverUri;

            {
                std::lock_guard<std::mutex> lock(m_clientMutex);
                if (!m_client)
                {
                    return VoidResult::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "MQTT client not initialized");
                }

                if (m_client->is_connected())
                {
                    alreadyConnected = true;
                }
                else
                {
                    serverUri = m_client->get_server_uri();
                    ELEGOO_LOG_INFO("Attempting MQTT connection to: {}", serverUri);
                    connectToken = m_client->connect(*m_connectOptions);
                }
            }

            if (alreadyConnected)
            {
                ELEGOO_LOG_INFO("MQTT client already connected");
                return VoidResult::Success();
            }

            // Update connection state and wait for connection to complete outside lock
            updateConnectionState(MqttConnectionState::CONNECTING);
            connectToken->wait_for(std::chrono::milliseconds(m_config.connectTimeoutMs));

            // Check connection result
            bool connectionSuccess = false;
            {
                std::lock_guard<std::mutex> lock(m_clientMutex);
                connectionSuccess = connectToken->is_complete() && m_client->is_connected();
                if (connectionSuccess)
                {
                    m_connectTime = std::chrono::steady_clock::now();
                }
            }

            if (connectionSuccess)
            {
                updateConnectionState(MqttConnectionState::CONNECTED);
                ELEGOO_LOG_INFO("MQTT connection established successfully to: {}", serverUri);
                return VoidResult::Success();
            }
            else
            {
                std::string errorMsg = "Connection timeout or failed to: " + serverUri;
                updateConnectionState(MqttConnectionState::CONNECT_FAILED, errorMsg);
                ELEGOO_LOG_ERROR("MQTT connection failed: {}", errorMsg);
                return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "MQTT connection timeout");
            }
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT connection failed: " + std::string(e.what());
            updateConnectionState(MqttConnectionState::CONNECT_FAILED, errorMsg);
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }
    }

    VoidResult MqttClient::Impl::disconnect()
    {
        try
        {
            mqtt::token_ptr disconnectToken;
            bool wasConnected = false;

            {
                std::lock_guard<std::mutex> lock(m_clientMutex);

                if (!m_client || !m_client->is_connected())
                {
                    return VoidResult::Success();
                }

                wasConnected = true;
                disconnectToken = m_client->disconnect();
            }

            if (wasConnected && disconnectToken)
            {
                disconnectToken->wait_for(5000ms);
                // Update connection state outside lock to avoid deadlock
                updateConnectionState(MqttConnectionState::DISCONNECTED);
                ELEGOO_LOG_INFO("MQTT connection disconnected");
            }

            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT disconnection failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }
    }

    bool MqttClient::Impl::isConnected() const
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        return m_client && m_client->is_connected();
    }

    MqttConnectionState MqttClient::Impl::getConnectionState() const
    {
        return m_connectionState.load();
    }

    VoidResult MqttClient::Impl::publish(const std::string &topic, const std::string &payload, int qos, bool retained)
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        try
        {
            if (!m_client || !m_client->is_connected())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "MQTT not connected");
            }

            auto msg = mqtt::make_message(topic, payload, qos, retained);
            auto pubToken = m_client->publish(msg);

            if (!pubToken->wait_for(10000ms))
            {
                return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "Message publish timeout");
            }

            ELEGOO_LOG_DEBUG("MQTT message published successfully: topic={}, payload_size={}", topic, payload.size());
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT message publish exception: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }
    }

    VoidResult MqttClient::Impl::publish(const MqttMessage &message)
    {
        return publish(message.topic, message.payload, message.qos, message.retained);
    }

    void MqttClient::Impl::publishAsync(const std::string &topic, const std::string &payload,
                                        int qos, bool retained, MqttPublishCallback callback)
    {
        try
        {
            mqtt::delivery_token_ptr pubToken;

            // Protect client operations
            {
                std::lock_guard<std::mutex> lock(m_clientMutex);
                if (!m_client || !m_client->is_connected())
                {
                    if (callback)
                    {
                        callback(false, topic, "MQTT not connected");
                    }
                    return;
                }

                auto msg = mqtt::make_message(topic, payload, qos, retained);
                pubToken = m_client->publish(msg, nullptr, *m_callbackHandler);
            }

            // Register callback (performed outside client lock)
            if (callback && pubToken)
            {
                std::lock_guard<std::mutex> lock(m_publishCallbackMutex);
                m_publishCallbacks[pubToken->get_message_id()] = callback;
            }

            ELEGOO_LOG_DEBUG("MQTT async message publish started: topic={}, payload_size={}", topic, payload.size());
        }
        catch (const std::exception &e)
        {
            if (callback)
            {
                callback(false, topic, "Publish exception: " + std::string(e.what()));
            }
            ELEGOO_LOG_ERROR("MQTT async message publish exception: {}", e.what());
        }
    }

    VoidResult MqttClient::Impl::subscribe(const std::string &topic, int qos)
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        try
        {
            if (!m_client || !m_client->is_connected())
            {
                ELEGOO_LOG_ERROR("MQTT client not connected, cannot subscribe to topic: {}", topic);
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "MQTT not connected");
            }

            ELEGOO_LOG_INFO("Attempting to subscribe to MQTT topic: {} with QoS: {}",  StringUtils::maskString(topic), qos);
            auto subToken = m_client->subscribe(topic, qos);

            if (!subToken->wait_for(5000ms))
            {
                ELEGOO_LOG_ERROR("MQTT subscription timeout for topic: {}", StringUtils::maskString(topic));
                return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "Subscribe timeout");
            }

            ELEGOO_LOG_INFO("MQTT topic subscribed successfully: topic={}, qos={}", StringUtils::maskString(topic), qos);
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT subscription exception: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }
    }

    VoidResult MqttClient::Impl::subscribe(const std::map<std::string, int> &topics)
    {
        try
        {
            if (!isConnected())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "MQTT not connected");
            }

            // Subscribe to each topic one by one
            for (const auto &pair : topics)
            {
                auto result = subscribe(pair.first, pair.second);
                if (!result.isSuccess())
                {
                    ELEGOO_LOG_WARN("Topic subscription failed: topic={}, error={}",  StringUtils::maskString(pair.first), result.message);
                }
            }

            ELEGOO_LOG_INFO("MQTT batch subscription completed: count={}", topics.size());
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT batch subscription exception: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }
    }

    VoidResult MqttClient::Impl::unsubscribe(const std::string &topic)
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        try
        {
            if (!m_client || !m_client->is_connected())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "MQTT not connected");
            }

            auto unsubToken = m_client->unsubscribe(topic);

            if (!unsubToken->wait_for(5000ms))
            {
                return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_TIMEOUT, "Unsubscribe timeout");
            }

            ELEGOO_LOG_INFO("MQTT topic unsubscribed successfully: topic={}",  StringUtils::maskString(topic));
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT unsubscribe exception: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }
    }

    VoidResult MqttClient::Impl::unsubscribe(const std::vector<std::string> &topics)
    {
        try
        {
            if (!isConnected())
            {
                return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, "MQTT not connected");
            }

            // Unsubscribe from each topic one by one
            for (const auto &topic : topics)
            {
                auto result = unsubscribe(topic);
                if (!result.isSuccess())
                {
                    ELEGOO_LOG_WARN("Topic unsubscription failed: topic={}, error={}",  StringUtils::maskString(topic), result.message);
                }
            }

            ELEGOO_LOG_INFO("MQTT batch unsubscription completed: count={}", topics.size());
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT batch unsubscription exception: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::NETWORK_ERROR, errorMsg);
        }
    }

    void MqttClient::Impl::setMessageCallback(MqttMessageCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_messageCallback = std::move(callback);
    }

    void MqttClient::Impl::setConnectionCallback(MqttConnectionCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_connectionCallback = std::move(callback);
    }

    VoidResult MqttClient::Impl::updateConfig(const MqttConfig &config)
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);

        if (m_client && m_client->is_connected())
        {
            return VoidResult::Error(ELINK_ERROR_CODE::OPERATION_IN_PROGRESS, "Please disconnect first before updating configuration");
        }

        try
        {
            m_config = config;

            // Recreate client
            std::string serverURI = ensureProtocolPrefix(m_config.brokerUrl);

            // auto createOptions = mqtt::create_options_builder()
            //                          .server_uri(serverURI)
            //                          .client_id(m_config.clientId)
            //                          .mqtt_version(MQTTVERSION_5)
            //                          .finalize();
            m_client = std::make_unique<mqtt::async_client>(serverURI, m_config.clientId);
            m_client->set_disconnected_handler([](const mqtt::properties &props, mqtt::ReasonCode code)
                                               { std::cout << "disconnected by broker, reason code: " << static_cast<int>(code) << std::endl; });
            m_client->set_callback(*m_callbackHandler);

            setupConnectOptions();

            ELEGOO_LOG_INFO("MQTT client configuration updated successfully");
            return VoidResult::Success();
        }
        catch (const std::exception &e)
        {
            std::string errorMsg = "MQTT configuration update failed: " + std::string(e.what());
            ELEGOO_LOG_ERROR(errorMsg);
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, errorMsg);
        }
    }

    const MqttConfig &MqttClient::Impl::getConfig() const
    {
        return m_config;
    }

    void MqttClient::Impl::updateConnectionState(MqttConnectionState newState, const std::string &message)
    {
        MqttConnectionState oldState = m_connectionState.exchange(newState);

        if (oldState != newState)
        {
            // Copy callback function to avoid race conditions when calling outside lock
            MqttConnectionCallback callback;
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                callback = m_connectionCallback;
            }

            // Call user callback outside lock to avoid deadlock
            if (callback)
            {
                try
                {
                    callback(newState, message);
                }
                catch (const std::exception &e)
                {
                    ELEGOO_LOG_ERROR("Error occurred while executing connection status callback: {}", e.what());
                }
            }
        }
    }

    void MqttClient::Impl::setupConnectOptions()
    {
        if (!m_client)
        {
            return;
        }

        m_connectOptions = std::make_unique<mqtt::connect_options>();

        // Explicitly specify using MQTT 5.0 protocol version
        m_connectOptions->v5();

        // Basic connection options
        m_connectOptions->set_clean_start(m_config.cleanStart);
        m_connectOptions->set_keep_alive_interval(m_config.keepAliveInterval);

        // m_connectOptions->set_connect_timeout(m_config.connectTimeoutMs / 1000); // Convert to seconds
        // Authentication information
        if (!m_config.username.empty())
        {
            m_connectOptions->set_user_name(m_config.username);
            if (!m_config.password.empty())
            {
                m_connectOptions->set_password(m_config.password);
            }
        }

        // Auto-reconnect
        if (m_config.enableAutoReconnect)
        {
            m_connectOptions->set_automatic_reconnect(1, m_config.maxReconnectInterval);
        }
        else
        {
            m_connectOptions->set_automatic_reconnect(false);
        }

        // SSL/TLS configuration for wss:// or mqtts://
        std::string serverURI = m_client->get_server_uri();
        if (requiresSSL(serverURI))
        {
            auto sslOptions = mqtt::ssl_options();
            // Enable SSL verification for production security
            // Set to true to verify server certificate (recommended for production)
            sslOptions.set_verify(m_config.enableSSL);
            // Use certificate files if provided in config
            if (!m_config.caCertPath.empty() && FileUtils::fileExists(m_config.caCertPath))
            {
                sslOptions.set_trust_store(m_config.caCertPath);
                sslOptions.set_error_handler([](const std::string &msg)
                                             { ELEGOO_LOG_ERROR("SSL Error: {}", msg); });
                ELEGOO_LOG_INFO("MQTT Using custom CA certificate: {}", m_config.caCertPath);
            }else{
                ELEGOO_LOG_WARN("MQTT CA certificate path is empty or file does not exist");
            }

            // if (!m_config.clientCertPath.empty() && !m_config.clientKeyPath.empty())
            // {
            //     sslOptions.set_key_store(m_config.clientCertPath);
            //     sslOptions.set_private_key(m_config.clientKeyPath);
            //     ELEGOO_LOG_INFO("Using client certificate authentication");
            // }

            m_connectOptions->set_ssl(sslOptions);
            ELEGOO_LOG_INFO("SSL/TLS enabled for secure connection: {}", serverURI);
        }
    }

    void MqttClient::Impl::notifyMessage(const std::string &topic, const MqttMessage &message)
    {
        // Copy callback function to avoid race conditions when calling outside lock
        MqttMessageCallback callback;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            callback = m_messageCallback;
        }

        // Call user callback outside lock to avoid deadlock
        if (callback)
        {
            try
            {
                callback(topic, message);
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Error occurred while executing message callback: {}", e.what());
            }
        }
    }

    // ==================== MqttClient Public Interface Implementation ====================

    MqttClient::MqttClient(const MqttConfig &config)
        : m_impl(std::make_unique<Impl>(config))
    {
    }

    MqttClient::~MqttClient() = default;

    VoidResult MqttClient::connect()
    {
        return m_impl->connect();
    }

    VoidResult MqttClient::disconnect()
    {
        return m_impl->disconnect();
    }

    bool MqttClient::isConnected() const
    {
        return m_impl->isConnected();
    }

    MqttConnectionState MqttClient::getConnectionState() const
    {
        return m_impl->getConnectionState();
    }

    VoidResult MqttClient::publish(const std::string &topic, const std::string &payload, int qos, bool retained)
    {
        return m_impl->publish(topic, payload, qos, retained);
    }

    VoidResult MqttClient::publish(const MqttMessage &message)
    {
        return m_impl->publish(message);
    }
    void MqttClient::publishAsync(const std::string &topic, const std::string &payload, int qos, bool retained, MqttPublishCallback callback)
    {
        m_impl->publishAsync(topic, payload, qos, retained, callback);
    }

    VoidResult MqttClient::subscribe(const std::string &topic, int qos)
    {
        return m_impl->subscribe(topic, qos);
    }

    VoidResult MqttClient::subscribe(const std::map<std::string, int> &topics)
    {
        return m_impl->subscribe(topics);
    }

    VoidResult MqttClient::unsubscribe(const std::string &topic)
    {
        return m_impl->unsubscribe(topic);
    }

    VoidResult MqttClient::unsubscribe(const std::vector<std::string> &topics)
    {
        return m_impl->unsubscribe(topics);
    }

    void MqttClient::setMessageCallback(MqttMessageCallback callback)
    {
        m_impl->setMessageCallback(std::move(callback));
    }

    void MqttClient::setConnectionCallback(MqttConnectionCallback callback)
    {
        m_impl->setConnectionCallback(std::move(callback));
    }

    VoidResult MqttClient::updateConfig(const MqttConfig &config)
    {
        return m_impl->updateConfig(config);
    }

    const MqttConfig &MqttClient::getConfig() const
    {
        return m_impl->getConfig();
    }

} // namespace elink
