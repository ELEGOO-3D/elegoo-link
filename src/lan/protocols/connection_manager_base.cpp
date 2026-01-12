#include "protocols/connection_manager_base.h"
#include <future>
namespace elink
{

    ConnectionManagerBase::ConnectionManagerBase(std::string protocolName)
        : hasValidConnectParams_(false), connected_(false), isConnecting_(false), shouldReconnect_(false),
          isReconnecting_(false),
          shouldStartDelayedReconnect_(false),
          autoReconnectEnabled_(true),
          protocolName_(protocolName)
    {
    }

    ConnectionManagerBase::~ConnectionManagerBase()
    {
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            statusCallback_ = nullptr;
        }
    }

    VoidResult ConnectionManagerBase::connect(const ConnectPrinterParams &connectParams, bool autoReconnect)
    {
        // Use mutex to protect connection operation
        std::lock_guard<std::mutex> lock(connectMutex_);

        // Store auto reconnect setting
        autoReconnectEnabled_ = autoReconnect;

        auto startTime = std::chrono::steady_clock::now();

        // 1. Check current connection status
        if (connected_.load() && isUnderlyingConnected())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::PRINTER_ALREADY_CONNECTED,
                "Printer already connected via " + getProtocolName() + ". Use disconnect() first if you want to reconnect.");
        }

        // 2. Check if connection is in progress
        if (isConnecting_.load())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::PRINTER_ALREADY_CONNECTED,
                "Another connection attempt is already in progress. Please wait for it to complete.");
        }

        // Set connection status
        isConnecting_ = true;

        // Save connection parameters (even if connection fails, save for reconnect)
        lastConnectParams_ = connectParams;
        hasValidConnectParams_ = true;

        try
        {
            // Call specific protocol's connect implementation
            VoidResult result = doConnect(connectParams);
            isConnecting_ = false;
            if (result.isSuccess())
            {
                connected_ = true;
                notifyStatusChange(true);
                ELEGOO_LOG_INFO("[{}] connected successfully", getProtocolName());
            }
            else
            {

                if (!connectParams.checkConnection)
                {
                    // Connection failed, start auto reconnect
                    startReconnectIfNeeded();
                }
            }

            return result;
        }
        catch (const std::exception &e)
        {
            isConnecting_ = false;

            // Start auto reconnect (exception)
            startReconnectIfNeeded();

            return VoidResult::Error(
                ELINK_ERROR_CODE::UNKNOWN_ERROR,
                "Internal error during " + getProtocolName() + " connection: " + std::string(e.what()));
        }
    }

    void ConnectionManagerBase::disconnect()
    {
        autoReconnectEnabled_ = false;
        shouldReconnect_ = false;
        shouldStartDelayedReconnect_ = false;

        bool wasConnected = connected_.exchange(false);
        if (wasConnected)
        {
            doDisconnect();
            ELEGOO_LOG_INFO("[{}] disconnected", getProtocolName());
        }

        // Call callback function outside the lock
        if (wasConnected)
        {
            notifyStatusChange(false);
        }


        delayedReconnectCondition_.notify_all();
        if (delayedReconnectTimer_.joinable())
        {
            delayedReconnectTimer_.join();
        }

        // Clean up reconnect thread
        cleanupReconnectThread();
    }

    bool ConnectionManagerBase::isConnected() const
    {
        return connected_.load() && isUnderlyingConnected();
    }

    void ConnectionManagerBase::setStatusCallback(std::function<void(bool)> callback)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        statusCallback_ = callback;
    }

    void ConnectionManagerBase::notifyStatusChange(bool connected)
    {
        std::function<void(bool)> callback;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            callback = statusCallback_;
        }

        if (callback)
        {
            callback(connected);
        }
    }

    void ConnectionManagerBase::startAutoReconnect()
    {
        connected_ = false;
        notifyStatusChange(false);
        startReconnectIfNeeded();
    }

    void ConnectionManagerBase::startReconnectIfNeeded()
    {
        // Check if auto reconnect is enabled
        if (!autoReconnectEnabled_)
        {
            return; // Auto reconnect is disabled
        }

        // Check reconnect conditions
        if (!hasValidConnectParams_.load())
        {
            return; // No valid connection parameters, cannot reconnect
        }

        if (isReconnecting_.load())
        {
            return; // Already reconnecting
        }

        if (isConnecting_.load())
        {
            return; // Currently connecting manually, do not start auto reconnect
        }

        // Check connection status
        if (connected_.load())
        {
            return; // Already connected, no need to reconnect
        }

        shouldReconnect_ = true;
        isReconnecting_ = true;

        // Clean up old reconnect thread
        cleanupReconnectThread();

        // Start a new reconnect thread
        reconnectThread_ = std::thread([this]()
                                       { reconnectLoop(); });
    }

    void ConnectionManagerBase::reconnectLoop()
    {
        while (shouldReconnect_.load())
        {
            // Check connection status
            if (connected_.load())
            {
                break; // Already connected, exit loop
            }

            // Wait for 5 seconds, can be interrupted
            {
                std::unique_lock<std::mutex> lock(reconnectMutex_);
                if (reconnectCondition_.wait_for(lock, std::chrono::milliseconds(5000), [this]
                                                 { return !shouldReconnect_.load(); }))
                {
                    break; // Interrupted, exit loop
                }
            } // Release reconnectMutex_ lock

            // Attempt reconnect - call connect() outside the lock to avoid deadlock
            ELEGOO_LOG_INFO("[{}] attempting automatic reconnection...", getProtocolName());

            try
            {
                auto result = connect(lastConnectParams_);
                if (result.isSuccess())
                {
                    ELEGOO_LOG_INFO("[{}] automatic reconnection successful", getProtocolName());
                    break; // Reconnection successful, exit loop
                }
                else
                {
                    ELEGOO_LOG_WARN("[{}] automatic reconnection failed: {}", getProtocolName(), result.message);
                }
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_WARN("[{}] automatic reconnection exception: {}", getProtocolName(), e.what());
            }
        }

        isReconnecting_ = false;
    }

    void ConnectionManagerBase::startDelayedAutoReconnect(int delayMs)
    {
        connected_ = false;
        // Check if auto reconnect is enabled
        if (!autoReconnectEnabled_)
        {
            return; // Auto reconnect is disabled
        }
        // Cancel previous delayed reconnect (if any)
        cancelDelayedReconnect();

        shouldStartDelayedReconnect_ = true;

        // Start delayed reconnect timer
        delayedReconnectTimer_ = std::thread([this, delayMs]()
                                             {
        std::unique_lock<std::mutex> lock(delayedReconnectMutex_);
        if (delayedReconnectCondition_.wait_for(lock, std::chrono::milliseconds(delayMs), [this] { 
            return !shouldStartDelayedReconnect_.load(); 
        })) {
            // Interrupted (connection may have recovered), do not start reconnect
            ELEGOO_LOG_DEBUG("[{}] delayed reconnect cancelled - connection recovered", getProtocolName());
            return;
        }
        
        // Delay expired, if reconnect is still needed and not connected, start reconnect
        if (shouldStartDelayedReconnect_.load()) {
            bool needReconnect = !connected_.load();
            
            if (needReconnect) {
                notifyStatusChange(false);
                ELEGOO_LOG_INFO("[{}] starting delayed reconnect after {}ms", getProtocolName(), delayMs);
                startReconnectIfNeeded();
            }
        } });
    }

    void ConnectionManagerBase::cancelDelayedReconnect()
    {
        if (delayedReconnectTimer_.joinable())
        {
            shouldStartDelayedReconnect_ = false;
            delayedReconnectCondition_.notify_all();
            delayedReconnectTimer_.join();
        }
    }

    void ConnectionManagerBase::notifyConnectionRecovered()
    {
        // Cancel pending delayed reconnect
        cancelDelayedReconnect();

        // Update connection status
        connected_ = true;

        ELEGOO_LOG_INFO("[{}] connection recovered - reconnection cancelled", getProtocolName());

        // Call callback function
        notifyStatusChange(true);
    }

    void ConnectionManagerBase::cleanupReconnectThread()
    {
        if (reconnectThread_.joinable())
        {
            reconnectCondition_.notify_all();
            reconnectThread_.join();
        }
    }

} // namespace elink
