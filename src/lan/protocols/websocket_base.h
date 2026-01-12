#ifndef WEBSOCKET_BASE_H
#define WEBSOCKET_BASE_H

#include "protocol_interface.h"
#include "protocols/connection_manager_base.h"
#include <ixwebsocket/IXWebSocket.h>
#include <functional>
#include <string>
#include <memory>
#include <mutex>

namespace elink
{
    /**
     * @brief WebSocket base class
     * 
     * Provides an extensible WebSocket implementation framework that supports subclass customization:
     * - Authentication logic
     * - Heartbeat mechanism
     * - Message handling
     * - Connection parameter configuration
     */
    class WebSocketBase : public IProtocol
    {
    public:
        WebSocketBase();
        virtual ~WebSocketBase();

        // IProtocol interface implementation
        VoidResult connect(const ConnectPrinterParams &connectParams, bool autoReconnect = true) override;
        void disconnect() override;
        bool isConnected() const override;
        bool sendCommand(const std::string &data) override;
        void setMessageCallback(std::function<void(const std::string &)> callback) override;
        void setConnectStatusCallback(std::function<void(bool)> callback) override;
        std::string getProtocolType() const override { return "websocket"; }

    protected:

        /**
         * @brief Process host into WebSocket connection URL
         * @param connectParams Connection parameters
         * @return Processed URL, empty string indicates processing failure
         */
        virtual std::string processConnectionUrl(const ConnectPrinterParams &connectParams) = 0;

        /**
         * @brief Custom WebSocket connection configuration
         * @param websocket WebSocket instance reference
         * @param printerInfo Printer information
         * @param connectParams Connection parameters
         */
        virtual void configureWebSocket(ix::WebSocket &websocket, 
                                       const ConnectPrinterParams &connectParams);

        /**
         * @brief Custom heartbeat configuration
         * @param websocket WebSocket instance reference
         * @return Returns true for custom heartbeat, false for default configuration
         */
        virtual bool configurePing(ix::WebSocket &websocket);

        /**
         * @brief Handle connection established event
         * @param msg WebSocket message
         * @return Returns true to continue processing, false to skip default handling
         */
        virtual bool onConnectionOpened(const ix::WebSocketMessagePtr &msg);

        /**
         * @brief Handle connection closed event
         * @param msg WebSocket message
         * @return Returns true to continue processing, false to skip default handling
         */
        virtual bool onConnectionClosed(const ix::WebSocketMessagePtr &msg);

        /**
         * @brief Handle connection error event
         * @param msg WebSocket message
         * @return Returns true to continue processing, false to skip default handling
         */
        virtual bool onConnectionError(const ix::WebSocketMessagePtr &msg);

        /**
         * @brief Handle received text message
         * @param message Message content
         * @return Returns true to continue processing, false to skip default handling
         */
        virtual bool onTextMessage(const std::string &message);

        /**
         * @brief Handle Ping message
         * @param msg WebSocket message
         * @return Returns true to continue processing, false to skip default handling
         */
        virtual bool onPingMessage(const ix::WebSocketMessagePtr &msg);

        /**
         * @brief Handle Pong message
         * @param msg WebSocket message
         * @return Returns true to continue processing, false to skip default handling
         */
        virtual bool onPongMessage(const ix::WebSocketMessagePtr &msg);

        /**
         * @brief Validate if connection is successfully established
         * @param websocket WebSocket instance reference
         * @return Returns true if connection validation succeeds
         */
        virtual bool validateConnection(const ix::WebSocket &websocket);

        /**
         * @brief Get connection timeout duration (seconds)
         * @return Timeout duration
         */
        virtual int getConnectionTimeoutSeconds() { return 8; }

        /**
         * @brief Get handshake timeout duration (seconds)
         * @return Handshake timeout duration
         */
        virtual int getHandshakeTimeoutSeconds() { return 5; }

        // ============ Custom heartbeat methods ============

        /**
         * @brief Check if custom heartbeat is enabled - can be overridden
         */
        virtual bool isHeartbeatEnabled() const;

        /**
         * @brief Get heartbeat interval in seconds - can be overridden
         */
        virtual int getHeartbeatIntervalSeconds() const;

        /**
         * @brief Create heartbeat message - can be overridden for custom heartbeat format
         */
        virtual std::string createHeartbeatMessage() const;

        /**
         * @brief Handle heartbeat response - can be overridden for custom heartbeat validation
         */
        virtual bool handleHeartbeatResponse(const std::string &message);

        /**
         * @brief Get heartbeat timeout in seconds - can be overridden
         */
        virtual int getHeartbeatTimeoutSeconds() const;

        // ============ Utility methods ============

        /**
         * @brief Get current WebSocket state
         * @return WebSocket ready state
         */
        ix::ReadyState getWebSocketState() const;

        /**
         * @brief Send raw WebSocket message
         * @param data Message data
         * @return Send result
         */
        ix::WebSocketSendInfo sendRawMessage(const std::string &data);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace elink

#endif // WEBSOCKET_BASE_H
