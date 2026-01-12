#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "type.h"
#include "utils/logger.h"
#include "protocol_interface.h"
namespace elink 
{

    /**
     * @brief Base Connection Manager
     *
     * Provides general connection lifecycle management features:
     * - Connection status management
     * - Auto-reconnect mechanism
     * - Thread-safe state operations
     * - Error handling and callback events
     *
     * Derived classes only need to implement specific connect/disconnect logic
     */
    class ConnectionManagerBase
    {
    public:
        /**
         * @brief Constructor
         */
        ConnectionManagerBase(std::string protocolName);

        /**
         * @brief Destructor - Cleans up all resources
         */
        virtual ~ConnectionManagerBase();

        /**
         * @brief Connect to the printer
         * @param connectParams Connection parameters
         * @param autoReconnect Whether to enable auto-reconnect (default is true)
         * @return Connection result
         */
        VoidResult connect(const ConnectPrinterParams &connectParams, bool autoReconnect = true);

        /**
         * @brief Disconnect
         */
        void disconnect();

        /**
         * @brief Check connection status
         * @return Whether connected
         */
        bool isConnected() const;

        /**
         * @brief Set connection status callback
         * @param callback Callback function
         */
        void setStatusCallback(std::function<void(bool)> callback);

    protected:
        /**
         * @brief Pure virtual function: Perform the actual connection operation
         * @param connectParams Connection parameters
         * @return Connection result
         */
        virtual VoidResult doConnect(const ConnectPrinterParams &connectParams) = 0;

        /**
         * @brief Pure virtual function: Perform the actual disconnection operation
         */
        virtual void doDisconnect() = 0;

        /**
         * @brief Pure virtual function: Check the underlying connection status
         * @return Whether connected
         */
        virtual bool isUnderlyingConnected() const = 0;

        /**
         * @brief Notify connection status change
         * @param connected New connection status
         */
        void notifyStatusChange(bool connected);

        /**
         * @brief Start auto-reconnect (called when connection is lost)
         */
        void startAutoReconnect();

        /**
         * @brief Start delayed auto-reconnect (handles quick connection recovery)
         * @param delayMs Delay time in milliseconds
         */
        void startDelayedAutoReconnect(int delayMs = 500);

        /**
         * @brief Cancel pending delayed reconnect
         */
        void cancelDelayedReconnect();

        /**
         * @brief Notify connection has recovered (cancel reconnect and update status)
         */
        void notifyConnectionRecovered();

        /**
         * @brief Get protocol name (used for logging)
         * @return Protocol name
         */
        std::string getProtocolName() const {
            return protocolName_;
        }

    private:
        /**
         * @brief Start reconnect mechanism (if needed)
         */
        void startReconnectIfNeeded();

        /**
         * @brief Reconnect loop logic
         */
        void reconnectLoop();

        /**
         * @brief Clean up reconnect thread
         */
        void cleanupReconnectThread();

    protected:
        // ============ Printer Information ============
        ConnectPrinterParams lastConnectParams_;
        std::atomic<bool> hasValidConnectParams_;
        std::atomic<bool> autoReconnectEnabled_;

        // ============ Connection Status Management ============
        std::atomic<bool> connected_;
        std::atomic<bool> isConnecting_;

        // ============ Callback Functions ============
        std::function<void(bool)> statusCallback_;
        mutable std::mutex callbackMutex_;

    private:
        // ============ Auto-Reconnect Mechanism ============
        std::atomic<bool> shouldReconnect_;
        std::atomic<bool> isReconnecting_;
        std::thread reconnectThread_;
        std::mutex reconnectMutex_;
        std::condition_variable reconnectCondition_;

        // ============ Delayed Reconnect Mechanism (handles quick recovery) ============
        std::atomic<bool> shouldStartDelayedReconnect_;
        std::thread delayedReconnectTimer_;
        std::mutex delayedReconnectMutex_;
        std::condition_variable delayedReconnectCondition_;

        // ============ Connection Operation Synchronization ============
        std::mutex connectMutex_;


        std::string protocolName_;
    };

} // namespace elink
