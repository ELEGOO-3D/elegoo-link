#ifndef MQTT_PROTOCOL_H
#define MQTT_PROTOCOL_H
#include "protocol_interface.h"
#include <mqtt/async_client.h>

namespace elink
{
    /**
     * Base MQTT Protocol Implementation
     * Provides a flexible architecture that allows inheritance for custom MQTT implementations.
     * Subclasses can override virtual methods to customize:
     * - Authentication logic
     * - Topic management
     * - Registration process
     * - Heartbeat mechanism
     * - Message handling
     */
    class MqttProtocol : public IProtocol
    {
    public:
        MqttProtocol();
        virtual ~MqttProtocol();

        // IProtocol interface implementation
        VoidResult connect(const ConnectPrinterParams &connectParams, bool autoReconnect = true) override;
        void disconnect() override;
        bool isConnected() const override;
        bool sendCommand(const std::string &data = "") override;
        void setMessageCallback(std::function<void(const std::string &)> callback) override;
        void setConnectStatusCallback(std::function<void(bool)> callback) override;
        std::string getProtocolType() const override { return "mqtt"; }

    protected:
    
        virtual std::string processConnectionUrl(const ConnectPrinterParams &connectParams) = 0;

        /**
         * Get MQTT client ID - can be overridden for custom client ID generation
         */
        virtual std::string getClientId(const ConnectPrinterParams & connectParams) const;

        /**
         * Validate connection parameters - can be overridden for custom parameter validation
         */
        virtual VoidResult validateConnectionParams(const ConnectPrinterParams &connectParams) const;

        /**
         * Configure MQTT connection options - can be overridden for custom authentication
         */
        virtual void configureConnectionOptions(mqtt::connect_options &conn_opts,
                                                const ConnectPrinterParams &connectParams);

        /**
         * Get subscription topics - can be overridden for custom topic management
         */
        virtual std::vector<std::string> getSubscriptionTopics(const ConnectPrinterParams & connectParams) const = 0;

        /**
         * Get command topic - can be overridden for custom command routing
         */
        virtual std::string getCommandTopic(const ConnectPrinterParams & connectParams, const std::string &commandType = "") const = 0;

        /**
         * Check if printer registration is required - can be overridden
         */
        virtual bool requiresRegistration() const;

        /**
         * Perform printer registration - can be overridden for custom registration logic
         */
        virtual bool performRegistration(const ConnectPrinterParams &connectParams,
                                         const std::string &clientId,
                                         std::function<bool(const std::string &, const std::string &)> sendMessageCallback);

        virtual bool isRegistrationMessage(const std::string &topic, const std::string &message) const;

        /**
         * Validate registration response - can be overridden
         */
        virtual bool validateRegistrationResponse(const std::string &topic,
                                                  const std::string &message,
                                                  const std::string &clientId,
                                                  ELINK_ERROR_CODE &errorCode,
                                                  std::string &errorMessage);

        /**
         * Get registration timeout in milliseconds - can be overridden
         */
        virtual int getRegistrationTimeoutMs() const;

        /**
         * Handle incoming MQTT message - can be overridden for custom message processing
         */
        virtual void handleMessage(const std::string &topic, const std::string &payload);

        /**
         * Check if heartbeat is enabled - can be overridden
         */
        virtual bool isHeartbeatEnabled() const;

        /**
         * Get heartbeat interval in seconds - can be overridden
         */
        virtual int getHeartbeatIntervalSeconds() const;

        /**
         * Create heartbeat message - can be overridden for custom heartbeat format
         */
        virtual std::string createHeartbeatMessage() const;

        /**
         * Handle heartbeat response - can be overridden for custom heartbeat validation
         */
        virtual bool handleHeartbeatResponse(const std::string &payload);

        /**
         * Get heartbeat topic - can be overridden
         */
        virtual std::string getHeartbeatTopic(const ConnectPrinterParams & connectParams) const;

        /**
         * Get heartbeat timeout in seconds - can be overridden
         */
        virtual int getHeartbeatTimeoutSeconds() const;

        mqtt::async_client *getMqttClient() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
#endif // MQTT_PROTOCOL_H
