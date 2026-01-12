#pragma once

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <optional>
#include "types/biz.h"

namespace elink
{
    /**
     * MQTT message structure
     */
    struct MqttMessage
    {
        std::string topic;              // Topic
        std::string payload;            // Message payload
        int qos = 1;                    // Quality of Service level (0, 1, 2)
        bool retained = false;          // Whether to retain message
    };

    /**
     * MQTT connection configuration
     */
    struct MqttConfig
    {
        std::string brokerUrl;         // MQTT broker server address
        std::string clientId;           // Client ID
        std::string username;           // Username (optional)
        std::string password;           // Password (optional)
        
        // MQTT 5.0 specific configuration
        int keepAliveInterval = 10;     // Heartbeat interval (seconds)
        bool cleanStart = true;         // Clean start
          
        // Connection configuration
        int connectTimeoutMs = 10000;   // Connection timeout (milliseconds)
        int maxReconnectInterval = 10;  // Maximum reconnection interval (seconds)
        bool enableAutoReconnect = false;// Whether to enable auto-reconnection

        bool enableSSL = true;        // Whether to enable SSL/TLS
        std::string caCertPath;        // CA certificate path for SSL/TLS (optional)
        std::string clientCertPath;    // Client certificate path for SSL/TLS (optional)
        std::string clientKeyPath;     // Client private key path for SSL/TLS (optional)
    };

    /**
     * MQTT connection state enumeration
     */
    enum class MqttConnectionState
    {
        DISCONNECTED = 0,    // Disconnected
        CONNECTING = 1,      // Connecting
        CONNECTED = 2,       // Connected
        RECONNECTING = 3,    // Reconnecting
        CONNECTION_LOST = 4, // Connection lost
        CONNECT_FAILED = 5   // Connection failed
    };

    /**
     * MQTT callback function type definitions
     */
    using MqttMessageCallback = std::function<void(const std::string& topic, const MqttMessage& message)>;
    using MqttConnectionCallback = std::function<void(MqttConnectionState state, const std::string& message)>;
    using MqttPublishCallback = std::function<void(bool success, const std::string& topic, const std::string& errorMessage)>;

    /**
     * MQTT client class
     * Encapsulates Paho MQTT C++, supports MQTT 5.0 protocol
     */
    class MqttClient
    {
    public:
        /**
         * Constructor
         * @param config MQTT connection configuration
         */
        explicit MqttClient(const MqttConfig& config);
        
        /**
         * Destructor
         */
        ~MqttClient();

        // ==================== Connection Management ====================
        
        /**
         * Connect to MQTT broker
         * @return Connection result
         */
        VoidResult connect();
        
        /**
         * Disconnect
         * @return Disconnection result  
         */
        VoidResult disconnect();
        
        /**
         * Check if connected
         * @return true if connected
         */
        bool isConnected() const;
        
        /**
         * Get current connection state
         * @return Connection state
         */
        MqttConnectionState getConnectionState() const;

        // ==================== Message Publishing ====================
        
        /**
         * Publish message (synchronous)
         * @param topic Topic
         * @param payload Message payload
         * @param qos Quality of Service level
         * @param retained Whether to retain message
         * @return Publishing result
         */
        VoidResult publish(const std::string& topic, const std::string& payload, 
                          int qos = 1, bool retained = false);
        
        /**
         * Publish message (using MqttMessage structure)
         * @param message Message structure
         * @return Publishing result
         */
        VoidResult publish(const MqttMessage& message);
        
        /**
         * Publish message asynchronously
         * @param topic Topic
         * @param payload Message payload
         * @param qos Quality of Service level
         * @param retained Whether to retain message
         * @param callback Publishing result callback
         */
        void publishAsync(const std::string& topic, const std::string& payload,
                         int qos, bool retained, MqttPublishCallback callback);

        // ==================== Message Subscription ====================
        
        /**
         * Subscribe to topic
         * @param topic Topic (supports wildcards)
         * @param qos Quality of Service level
         * @return Subscription result
         */
        VoidResult subscribe(const std::string& topic, int qos = 1);
        
        /**
         * Subscribe to multiple topics
         * @param topics Topic and QoS mapping
         * @return Subscription result
         */
        VoidResult subscribe(const std::map<std::string, int>& topics);
        
        /**
         * Unsubscribe from topic
         * @param topic Topic
         * @return Unsubscription result
         */
        VoidResult unsubscribe(const std::string& topic);
        
        /**
         * Unsubscribe from multiple topics
         * @param topics Topic list
         * @return Unsubscription result
         */
        VoidResult unsubscribe(const std::vector<std::string>& topics);

        // ==================== Callback Settings ====================
        
        /**
         * Set message receive callback
         * @param callback Message callback function
         */
        void setMessageCallback(MqttMessageCallback callback);
        
        /**
         * Set connection state callback
         * @param callback Connection state callback function
         */
        void setConnectionCallback(MqttConnectionCallback callback);

        // ==================== Configuration Management ====================
        
        /**
         * Update configuration
         * @param config New configuration
         * @return Update result
         */
        VoidResult updateConfig(const MqttConfig& config);
        
        /**
         * Get current configuration
         * @return Current configuration
         */
        const MqttConfig& getConfig() const;

    public: // Changed from private to allow friend class access
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace elink
