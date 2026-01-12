#pragma once

#include <string>
#include <functional>
#include <memory>
#include <map>
#include <vector>
#include "type.h"
#include "types/internal/internal.h"
namespace elink 
{

    /**
     * Base class for communication protocol interfaces
     * Supports different protocols such as MQTT, WebSocket, etc.
     */
    class IProtocol
    {
    public:
        virtual ~IProtocol() = default;

        /**
         * Connect to the printer
         * @param connectParams Connection parameters
         * @param autoReconnect Whether to automatically reconnect, default is true
         * @return VoidResult Connection result, including detailed success/failure information
         */
        virtual VoidResult connect(const ConnectPrinterParams &connectParams, bool autoReconnect = true) = 0;

        /**
         * Disconnect
         */
        virtual void disconnect() = 0;

        /**
         * Check if connected
         * @return true if connected
         */
        virtual bool isConnected() const = 0;

        /**
         * Send a command
         * @param data Command data
         * @return true if successful
         */
        virtual bool sendCommand(const std::string &data = "") = 0;

        /**
         * Set the message reception callback
         * @param callback Callback function
         */
        virtual void setMessageCallback(std::function<void(const std::string &)> callback) = 0;

        /**
         * Set the connection status callback
         * @param callback Callback function
         */
        virtual void setConnectStatusCallback(std::function<void(bool)> callback) = 0;

        /**
         * Get the protocol type
         * @return Protocol type string
         */
        virtual std::string getProtocolType() const = 0;
    };
} // namespace elink
