#include "protocols/websocket_base.h"
#include "protocols/connection_manager_base.h"
#include "protocols/error_handler.h"
#include "utils/logger.h"
#include "utils/utils.h"
// Prevent Windows headers from defining max/min macros
#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#include <iostream>
#include <thread>
#include <chrono>

namespace elink
{
    /**
     * @brief Internal implementation of WebSocket base class
     */
    class WebSocketBase::Impl : public ConnectionManagerBase
    {
    public:
        Impl(WebSocketBase *parent)
            : ConnectionManagerBase("WEBSOCKET"),
              parent_(parent),
              connectionError_(),
              connectionFailed_(false),
              heartbeatRunning_(false),
              lastPongReceived_(std::chrono::steady_clock::now())
        {
            ix::initNetSystem();
        }

        ~Impl()
        {
            {
                std::lock_guard<std::mutex> lock(websocketMutex_);
                websocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {});
            }
            stopHeartbeat();
            disconnect();
        }

        /**
         * @brief Send command to WebSocket server
         */
        bool sendCommand(const std::string &data)
        {
            if (!isConnected())
            {
                ELEGOO_LOG_ERROR("[{}] WebSocket not connected", lastConnectParams_.host);
                return false;
            }

            ELEGOO_LOG_DEBUG("[{}] Sending command: {}", lastConnectParams_.host, data);

            try
            {
                std::lock_guard<std::mutex> lock(websocketMutex_);
                auto result = websocket_.send(data);
                return result.success;
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[{}] WebSocket send command failed: {}", lastConnectParams_.host, e.what());
                return false;
            }
        }

        /**
         * @brief Send raw WebSocket message
         */
        ix::WebSocketSendInfo sendRawMessage(const std::string &data)
        {
            std::lock_guard<std::mutex> lock(websocketMutex_);
            return websocket_.send(data);
        }

        /**
         * @brief Get WebSocket status
         */
        ix::ReadyState getWebSocketState() const
        {
            std::lock_guard<std::mutex> lock(websocketMutex_);
            return websocket_.getReadyState();
        }

        /**
         * @brief Set message receive callback function
         */
        void setMessageCallback(std::function<void(const std::string &)> callback)
        {
            std::lock_guard<std::mutex> lock(messageMutex_);
            messageCallback_ = callback;
        }

    protected:
        /**
         * @brief Implement specific WebSocket connection logic
         */
        VoidResult doConnect(const ConnectPrinterParams &connectParams) override
        {
            auto startTime = std::chrono::steady_clock::now();

            // 1. Validate WebSocket URL
            std::string url = connectParams.host;
            if (url.empty())
            {
                return ErrorHandler::createConnectionFailure(
                    ELINK_ERROR_CODE::INVALID_PARAMETER,
                    "WebSocket URL is empty",
                    "Connection URL parameter is missing or empty",
                    startTime);
            }

            ELEGOO_LOG_DEBUG("WebSocket connecting to: {}", url);

            // 2. Let subclass handle URL (e.g., add authentication token)
            std::string processedUrl = parent_->processConnectionUrl(connectParams);
            if (processedUrl.empty())
            {
                return ErrorHandler::createConnectionFailure(
                    ELINK_ERROR_CODE::INVALID_PARAMETER,
                    "Invalid processed URL",
                    "Failed to process connection URL",
                    startTime);
            }

            // 3. Configure WebSocket
            {
                std::lock_guard<std::mutex> lock(websocketMutex_);
                websocket_.setUrl(processedUrl);
                websocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg)
                                                { handleMessage(msg); });

                // 4. Let subclass configure WebSocket parameters
                parent_->configureWebSocket(websocket_, connectParams);
                websocket_.disableAutomaticReconnection();
                // 5. Configure heartbeat (customizable by subclass)
                if (!parent_->configurePing(websocket_))
                {
                    // Use default heartbeat configuration
                    // websocket_.setHandshakeTimeout(parent_->getHandshakeTimeoutSeconds());
                    // websocket_.setPingInterval(30);
                    // websocket_.setPingMessage("ping");
                }

                // 6. Start connection
                connectionFailed_ = false;
                connectionError_.clear();
                websocket_.start();
            }

            // 7. Wait for connection establishment
            auto connectionStart = std::chrono::steady_clock::now();
            int connectionTimeout = parent_->getConnectionTimeoutSeconds() * 1000;
            if (connectParams.connectionTimeout > 0)
            {
                connectionTimeout = connectParams.connectionTimeout;
            }
            const auto timeout = std::chrono::milliseconds(connectionTimeout);

            while (!isUnderlyingConnected() && !connectionFailed_ &&
                   (std::chrono::steady_clock::now() - connectionStart) < timeout)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime);

            // 8. Check connection result
            if (connectionFailed_)
            {
                {
                    std::lock_guard<std::mutex> lock(websocketMutex_);
                    websocket_.stop();
                }
                ELINK_ERROR_CODE errorCode = ErrorHandler::mapWebSocketErrorCode(connectionError_);

                return VoidResult::Error(errorCode, connectionError_);
            }

            if (!isUnderlyingConnected())
            {
                {
                    std::lock_guard<std::mutex> lock(websocketMutex_);
                    websocket_.stop();
                }
                return ErrorHandler::createTimeoutFailure("WebSocket", startTime);
            }

            // 9. Let subclass validate connection
            bool isValidConnection;
            {
                std::lock_guard<std::mutex> lock(websocketMutex_);
                isValidConnection = parent_->validateConnection(websocket_);
            }
            if (!isValidConnection)
            {
                {
                    std::lock_guard<std::mutex> lock(websocketMutex_);
                    websocket_.stop();
                }
                return ErrorHandler::createConnectionFailure(
                    ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR,
                    "Connection validation failed",
                    "Custom connection validation returned false",
                    startTime);
            }

            ELEGOO_LOG_INFO("WebSocket connected successfully to {} (duration: {}ms)",
                            processedUrl, duration.count());

            // Start heartbeat if enabled
            if (parent_->isHeartbeatEnabled())
            {
                startHeartbeat();
            }

            return VoidResult::Success();
        }

        /**
         * @brief Implement specific WebSocket disconnection logic
         */
        void doDisconnect() override
        {
            stopHeartbeat();
            std::lock_guard<std::mutex> lock(websocketMutex_);
            websocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {});
            if (websocket_.getReadyState() == ix::ReadyState::Open)
            {
                websocket_.stop();
            }
        }

        /**
         * @brief Check underlying WebSocket connection status
         */
        bool isUnderlyingConnected() const override
        {
            std::lock_guard<std::mutex> lock(websocketMutex_);
            return websocket_.getReadyState() == ix::ReadyState::Open;
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
                ELEGOO_LOG_DEBUG("[{}] WebSocket heartbeat started", lastConnectParams_.host);
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
                ELEGOO_LOG_DEBUG("[{}] WebSocket heartbeat stopped", lastConnectParams_.host);
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
                if (!isUnderlyingConnected())
                {
                    ELEGOO_LOG_WARN("[{}] WebSocket heartbeat: connection lost, stopping heartbeat", lastConnectParams_.host);
                    break;
                }

                // Send heartbeat using virtual method
                if (!sendHeartbeat())
                {
                    ELEGOO_LOG_ERROR("[{}] WebSocket heartbeat: failed to send heartbeat", lastConnectParams_.host);
                    continue;
                }

                // // Check heartbeat response timeout
                // auto now = std::chrono::steady_clock::now();
                // auto timeSinceLastResponse = std::chrono::duration_cast<std::chrono::seconds>(
                //     now - lastPongReceived_);

                // int timeoutSeconds = parent_->getHeartbeatTimeoutSeconds();
                // if (timeSinceLastResponse > std::chrono::seconds(timeoutSeconds))
                // {
                //     ELEGOO_LOG_ERROR("WebSocket heartbeat: response timeout, last response {} seconds ago",
                //                      timeSinceLastResponse.count());
                //     // Trigger reconnection
                //     // startDelayedAutoReconnect(500);
                //     break;
                // }
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
                if (!isUnderlyingConnected())
                {
                    return false;
                }

                std::string heartbeatMessage = parent_->createHeartbeatMessage();
                ELEGOO_LOG_DEBUG("[{}] Sending WebSocket heartbeat: {}", lastConnectParams_.host, heartbeatMessage);

                std::lock_guard<std::mutex> lock(websocketMutex_);
                auto result = websocket_.send(heartbeatMessage);
                return result.success;
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[{}] WebSocket send heartbeat failed: {}", lastConnectParams_.host, e.what());
                return false;
            }
        }

        /**
         * @brief WebSocket message handler
         */
        void handleMessage(const ix::WebSocketMessagePtr &msg)
        {
            try
            {
                switch (msg->type)
                {
                case ix::WebSocketMessageType::Open:
                    if (parent_->onConnectionOpened(msg))
                    {
                        ELEGOO_LOG_INFO("[{}] WebSocket connection opened", lastConnectParams_.host);
                        // notifyConnectionRecovered();
                        connectionError_.clear();
                        connectionFailed_ = false;
                    }
                    break;

                case ix::WebSocketMessageType::Close:
                    if (parent_->onConnectionClosed(msg))
                    {
                        ELEGOO_LOG_INFO("[{}] WebSocket connection closed: {}", lastConnectParams_.host, msg->closeInfo.reason);
                        // startDelayedAutoReconnect(500);
                        startAutoReconnect();
                    }
                    break;

                case ix::WebSocketMessageType::Message:
                    ELEGOO_LOG_DEBUG("[{}] WebSocket message received: {}", lastConnectParams_.host, msg->str);
                    if (parent_->onTextMessage(msg->str))
                    {
                        // Check if this is a heartbeat response
                        if (parent_->isHeartbeatEnabled() && parent_->handleHeartbeatResponse(msg->str))
                        {
                            std::lock_guard<std::mutex> lock(heartbeatMutex_);
                            lastPongReceived_ = std::chrono::steady_clock::now();
                            ELEGOO_LOG_DEBUG("[{}] WebSocket heartbeat response received", lastConnectParams_.host);
                            return; // Don't pass heartbeat messages to business layer
                        }

                        handleTextMessage(msg->str);
                    }
                    break;

                case ix::WebSocketMessageType::Error:
                    if (parent_->onConnectionError(msg))
                    {
                        ELEGOO_LOG_ERROR("[{}] WebSocket error: {}", lastConnectParams_.host, msg->errorInfo.reason);
                        connectionError_ = "WebSocket error: " + msg->errorInfo.reason +
                                           " (HTTP status: " + std::to_string(msg->errorInfo.http_status) + ")";
                        connectionFailed_ = true;
                        // startDelayedAutoReconnect(500);
                    }
                    break;

                case ix::WebSocketMessageType::Pong:
                    if (parent_->onPongMessage(msg))
                    {
                        ELEGOO_LOG_DEBUG("[{}] WebSocket pong received: {}", lastConnectParams_.host, msg->str);
                    }
                    break;

                case ix::WebSocketMessageType::Ping:
                    if (parent_->onPingMessage(msg))
                    {
                        ELEGOO_LOG_DEBUG("[{}] WebSocket ping received: {}", lastConnectParams_.host, msg->str);
                    }
                    break;

                default:
                    ELEGOO_LOG_WARN("[{}] WebSocket received unknown message type: {}",
                                    lastConnectParams_.host, static_cast<int>(msg->type));
                    break;
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[{}] Error handling WebSocket message: {}", lastConnectParams_.host, e.what());
            }
        }

        /**
         * @brief Handle received text messages
         */
        void handleTextMessage(const std::string &message)
        {
            try
            {
                if (message.empty())
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(messageMutex_);
                if (messageCallback_)
                {
                    messageCallback_(message);
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("[{}] Error handling WebSocket text message: {}", lastConnectParams_.host, e.what());
            }
        }

    private:
        // ============ WebSocket related ============
        WebSocketBase *parent_;
        ix::WebSocket websocket_;
        mutable std::mutex websocketMutex_;  // Protect websocket_ operations
        std::string connectionError_;
        bool connectionFailed_;

        // ============ Heartbeat related ============
        std::atomic<bool> heartbeatRunning_;
        std::thread heartbeatThread_;
        std::chrono::steady_clock::time_point lastPongReceived_;
        mutable std::mutex heartbeatMutex_;

        // ============ Callback functions ============
        std::function<void(const std::string &)> messageCallback_;
        std::mutex messageMutex_;
    };

    // ============ WebSocketBase public interface implementation ============

    WebSocketBase::WebSocketBase() : impl_(std::make_unique<Impl>(this))
    {
    }

    WebSocketBase::~WebSocketBase() = default;

    VoidResult WebSocketBase::connect(const ConnectPrinterParams &connectParams, bool autoReconnect)
    {
        return impl_->connect(connectParams, autoReconnect);
    }

    void WebSocketBase::disconnect()
    {
        impl_->disconnect();
    }

    bool WebSocketBase::isConnected() const
    {
        return impl_->isConnected();
    }

    bool WebSocketBase::sendCommand(const std::string &data)
    {
        return impl_->sendCommand(data);
    }

    void WebSocketBase::setMessageCallback(std::function<void(const std::string &)> callback)
    {
        impl_->setMessageCallback(callback);
    }

    void WebSocketBase::setConnectStatusCallback(std::function<void(bool)> callback)
    {
        impl_->setStatusCallback(callback);
    }

    ix::ReadyState WebSocketBase::getWebSocketState() const
    {
        return impl_->getWebSocketState();
    }

    ix::WebSocketSendInfo WebSocketBase::sendRawMessage(const std::string &data)
    {
        return impl_->sendRawMessage(data);
    }

    // ============ Default hook method implementations ============

    void WebSocketBase::configureWebSocket(ix::WebSocket &websocket,
                                           const ConnectPrinterParams &connectParams)
    {
        // Default implementation: no additional configuration
    }

    bool WebSocketBase::configurePing(ix::WebSocket &websocket)
    {
        // Default implementation: return false, use default heartbeat configuration
        return false;
    }

    bool WebSocketBase::onConnectionOpened(const ix::WebSocketMessagePtr &msg)
    {
        // Default implementation: continue processing
        return true;
    }

    bool WebSocketBase::onConnectionClosed(const ix::WebSocketMessagePtr &msg)
    {
        // Default implementation: continue processing
        return true;
    }

    bool WebSocketBase::onConnectionError(const ix::WebSocketMessagePtr &msg)
    {
        // Default implementation: continue processing
        return true;
    }

    bool WebSocketBase::onTextMessage(const std::string &message)
    {
        // Default implementation: continue processing
        return true;
    }

    bool WebSocketBase::onPingMessage(const ix::WebSocketMessagePtr &msg)
    {
        // Default implementation: continue processing
        return true;
    }

    bool WebSocketBase::onPongMessage(const ix::WebSocketMessagePtr &msg)
    {
        // Default implementation: continue processing
        return true;
    }

    bool WebSocketBase::validateConnection(const ix::WebSocket &websocket)
    {
        // Default implementation: check WebSocket status
        return websocket.getReadyState() == ix::ReadyState::Open;
    }

    // ============ Default heartbeat method implementations ============

    bool WebSocketBase::isHeartbeatEnabled() const
    {
        // Default implementation: disable heartbeat
        return false;
    }

    int WebSocketBase::getHeartbeatIntervalSeconds() const
    {
        // Default implementation: 30 seconds interval
        return 20;
    }

    std::string WebSocketBase::createHeartbeatMessage() const
    {
        // Default implementation: send ping string
        return "ping";
    }

    bool WebSocketBase::handleHeartbeatResponse(const std::string &message)
    {
        // Default implementation: check if message contains "pong"
        return message.find("pong") != std::string::npos;
    }

    int WebSocketBase::getHeartbeatTimeoutSeconds() const
    {
        // Default implementation: 60 seconds timeout
        return 62;
    }

} // namespace elink
