#include "discovery/printer_discovery.h"
#include "adapters/elegoo_cc_adapters.h"
#include "adapters/elegoo_cc2_adapters.h"
#include "adapters/generic_moonraker_adapters.h"
#include "utils/utils.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <algorithm>
#include <set>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

namespace elink
{

    PrinterDiscovery::PrinterDiscovery()
        : isDiscovering_(false), shouldStop_(false), udpSocket_(INVALID_SOCKET)
    {

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        // Create discovery strategies directly
        addDiscoveryStrategy(getDiscoveryStrategy(PrinterType::ELEGOO_FDM_CC));
        addDiscoveryStrategy(getDiscoveryStrategy(PrinterType::ELEGOO_FDM_CC2));
        addDiscoveryStrategy(getDiscoveryStrategy(PrinterType::GENERIC_FDM_KLIPPER));

        ELEGOO_LOG_INFO("PrinterDiscovery initialized with {} strategies", discoveryStrategies_.size());
    }

    std::unique_ptr<IDiscoveryStrategy> PrinterDiscovery::getDiscoveryStrategy(PrinterType printerType)
    {
        switch (printerType)
        {
        case PrinterType::ELEGOO_FDM_CC:
            return std::make_unique<ElegooFdmCCDiscoveryStrategy>();
        case PrinterType::ELEGOO_FDM_CC2:
            return std::make_unique<ElegooFdmCC2DiscoveryStrategy>();
        case PrinterType::ELEGOO_FDM_KLIPPER:
        case PrinterType::GENERIC_FDM_KLIPPER:
            return std::make_unique<GenericMoonrakerDiscoveryStrategy>();
        default:
            return nullptr;
        }
    }

    PrinterDiscovery::~PrinterDiscovery()
    {
        try
        {
            // Ensure discovery is stopped first, wait for thread to completely end
            stopDiscovery();

            // Extra ensure all resources are cleaned up
            closeUdpSocket();

#ifdef _WIN32
            // Ensure WinSock cleanup after all socket operations are complete
            WSACleanup();
#endif

            // discoveryThread_ already joined in stopDiscovery(), no need to join again

            ELEGOO_LOG_INFO("PrinterDiscovery destroyed successfully");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Exception in PrinterDiscovery destructor: {}", e.what());
        }
        catch (...)
        {
            ELEGOO_LOG_ERROR("Unknown exception in PrinterDiscovery destructor");
        }
    }

    void PrinterDiscovery::addDiscoveryStrategy(std::unique_ptr<IDiscoveryStrategy> strategy)
    {
        discoveryStrategies_.push_back(std::move(strategy));
    }

    bool PrinterDiscovery::startDiscovery(const DiscoveryConfig &config,
                                         PrinterDiscoveredCallback callback,
                                         DiscoveryCompletionCallback completionCallback)
    {
        if (isDiscovering_)
        {
            ELEGOO_LOG_ERROR("Discovery already running");
            return false;
        }

        if (discoveryStrategies_.empty())
        {
            ELEGOO_LOG_ERROR("No discovery strategies available");
            return false;
        }

        // Validate configuration parameters
        if (config.timeoutMs <= 0 || config.timeoutMs > 300000) // Max 5 minutes
        {
            ELEGOO_LOG_ERROR("Invalid timeout value: {}ms (must be 1-300000)", config.timeoutMs);
            return false;
        }
        if (config.enableAutoRetry && config.broadcastInterval >= config.timeoutMs)
        {
            ELEGOO_LOG_ERROR("Broadcast interval ({}ms) must be less than timeout ({}ms)",
                           config.broadcastInterval, config.timeoutMs);
            return false;
        }

        // Thread-safe callback assignment
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            discoveryCallback_ = callback;
            completionCallback_ = completionCallback;
        }
        config_ = config;

        // Clear and reset port list
        ports_.clear();

        // Use strategy's default ports, automatically deduplicate
        std::set<int> uniquePorts;
        for (const auto &strategy : discoveryStrategies_)
        {
            int defaultPort = strategy->getDefaultPort();
            uniquePorts.insert(defaultPort);
        }

        // Convert deduplicated ports to vector
        ports_.assign(uniquePorts.begin(), uniquePorts.end());

        shouldStop_ = false;

        clearDiscoveredPrinters();
        isDiscovering_ = true;
        // Clean up discoveryThread_
        if (discoveryThread_.joinable())
        {
            discoveryThread_.join();
        }

        // Start discovery thread
        discoveryThread_ = std::thread(&PrinterDiscovery::discoveryThread, this);

        ELEGOO_LOG_INFO("Printer discovery started with {} strategies on {} ports",
                        discoveryStrategies_.size(), ports_.size());

        return true;
    }

    std::vector<PrinterInfo> PrinterDiscovery::discoverPrintersSync(const DiscoveryConfig &config)
    {
        std::mutex completionMutex;
        std::condition_variable completionCv;
        bool completed = false;
        std::vector<PrinterInfo> result;

        auto callback = [&](const PrinterInfo &printer)
        {
            // No special handling in callback, printer will be automatically added to internal list
        };

        auto completionCallback = [&](const std::vector<PrinterInfo> &printers)
        {
            std::lock_guard<std::mutex> lock(completionMutex);
            result = printers;
            completed = true;
            completionCv.notify_one();
        };

        if (startDiscovery(config, callback, completionCallback))
        {
            // Wait for discovery completion
            std::unique_lock<std::mutex> lock(completionMutex);
            completionCv.wait_for(lock, std::chrono::milliseconds(config.timeoutMs + 1000), [&]
                                  { return completed; });

            if (!completed)
            {
                // Timeout occurred, stop discovery and get current results
                stopDiscovery();
                result = getDiscoveredPrinters();
            }
        }

        return result;
    }

    void PrinterDiscovery::stopDiscovery()
    {
        // Set stop flag first (even if not discovering, in case thread is still running)
        shouldStop_ = true;

        // Always try to join the thread if it's joinable, regardless of isDiscovering_ state
        // This handles the race condition where isDiscovering_ is set to false but thread hasn't exited yet
        if (discoveryThread_.joinable())
        {
            discoveryThread_.join();
        }

        // Ensure state is correct
        isDiscovering_ = false;

        ELEGOO_LOG_INFO("Printer discovery stopped");
    }

    bool PrinterDiscovery::isDiscovering() const
    {
        return isDiscovering_;
    }

    std::vector<PrinterInfo> PrinterDiscovery::getDiscoveredPrinters() const
    {
        std::lock_guard<std::mutex> lock(printersMutex_);
        return discoveredPrinters_;
    }

    void PrinterDiscovery::clearDiscoveredPrinters()
    {
        std::lock_guard<std::mutex> lock(printersMutex_);
        discoveredPrinters_.clear();
        discoveredPrinterIds_.clear();
    }
    void PrinterDiscovery::discoveryThread()
    {
        try
        {
            auto startTime = std::chrono::steady_clock::now();
            auto lastBroadcastTime = startTime;
            auto timeout = std::chrono::milliseconds(config_.timeoutMs);

            if (!createUdpSocket())
            {
                ELEGOO_LOG_ERROR("Failed to create UDP socket: {}", getLastSocketError());
                // Clean up state and exit
                cleanupDiscoveryState();
                return;
            }

            // Bind to receive port, try multiple ports to avoid conflicts
            if (!bindToAvailablePort())
            {
                ELEGOO_LOG_ERROR("Failed to bind UDP socket to any available port: {}", getLastSocketError());
                // Clean up state and exit
                cleanupDiscoveryState();
                return;
            }

            // Send initial broadcast
            sendBroadcastToAllPorts();

            // Main loop
            while (!shouldStop_ && std::chrono::steady_clock::now() - startTime < timeout)
            {

                // Receive responses
                if (receiveResponses())
                {
                    // Process received data
                }

                // Periodically resend broadcast
                auto currentTime = std::chrono::steady_clock::now();
                if (config_.enableAutoRetry &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastBroadcastTime).count() >= config_.broadcastInterval)
                {
                    ELEGOO_LOG_DEBUG("Re-sending discovery broadcast...");
                    sendBroadcastToAllPorts();
                    lastBroadcastTime = currentTime;
                }
            }

            ELEGOO_LOG_DEBUG("Discovery thread completed normally");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error in discovery thread: {}", e.what());
        }

        // Whether ending normally or abnormally, state must be cleaned up
        cleanupDiscoveryState();
    }

    bool PrinterDiscovery::receiveResponses()
    {
        char buffer[4096];
        sockaddr_in senderAddr;
        socklen_t senderLen = sizeof(senderAddr);

        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(udpSocket_, &readFds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms

#ifdef _WIN32
        int activity = select(0, &readFds, nullptr, nullptr, &tv);
#else
        // Mac/Linux needs to pass maximum file descriptor + 1
        int activity = select(udpSocket_ + 1, &readFds, nullptr, nullptr, &tv);
#endif

        if (activity > 0 && FD_ISSET(udpSocket_, &readFds))
        {
            int bytesReceived = recvfrom(udpSocket_, buffer, sizeof(buffer) - 1, 0,
                                         (struct sockaddr *)&senderAddr, &senderLen);

            if (bytesReceived > 0)
            {
                buffer[bytesReceived] = '\0';
                std::string senderIp = inet_ntoa(senderAddr.sin_addr);
                int senderPort = ntohs(senderAddr.sin_port);

                processUdpResponse(buffer, senderIp, senderPort);
                return true;
            }
            else if (bytesReceived == SOCKET_ERROR)
            {
#ifdef _WIN32
                int error = WSAGetLastError();
                ELEGOO_LOG_DEBUG("recvfrom failed with error: {}", error);
#else
                ELEGOO_LOG_DEBUG("recvfrom failed with error: {} ({})", errno, strerror(errno));
#endif
            }
        }
        else if (activity == SOCKET_ERROR)
        {
#ifdef _WIN32
            int error = WSAGetLastError();
            ELEGOO_LOG_ERROR("select failed with error: {}", error);
#else
            ELEGOO_LOG_ERROR("select failed with error: {} ({})", errno, strerror(errno));
#endif
        }

        return false;
    }

    void PrinterDiscovery::sendBroadcastToAllPorts()
    {
        for (const auto &strategy : discoveryStrategies_)
        {
            std::string message = strategy->getDiscoveryMessage();
            int defaultPort = strategy->getDefaultPort();

            // Check if this strategy's default port is in current port list
            if (std::find(ports_.begin(), ports_.end(), defaultPort) != ports_.end())
            {
                sendDiscoveryBroadcast(defaultPort, message);
            }
        }
    }

    void PrinterDiscovery::processUdpResponse(const std::string &data, const std::string &senderIp, int senderPort)
    {
        ELEGOO_LOG_DEBUG("Received response from {}:{}", senderIp, senderPort);
        ELEGOO_LOG_DEBUG("Response data: {}", data);

        // Try to parse response with each strategy
        for (const auto &strategy : discoveryStrategies_)
        {
            auto printerInfo = strategy->parseResponse(data, senderIp, senderPort);
            if (printerInfo)
            {
                // Check if this printer has already been discovered (use set for O(1) lookup)
                bool isNew = false;
                {
                    std::lock_guard<std::mutex> lock(printersMutex_);
                    // Try to insert printer ID, if insertion succeeds it's a new printer
                    if (discoveredPrinterIds_.insert(printerInfo->printerId).second)
                    {
                        discoveredPrinters_.push_back(*printerInfo);
                        isNew = true;
                    }
                }

                if (isNew)
                {
                    // Notify callback (call outside lock to avoid deadlock)
                    PrinterDiscoveredCallback callback;
                    {
                        std::lock_guard<std::mutex> lock(callbackMutex_);
                        callback = discoveryCallback_;
                    }
                    if (callback)
                    {
                        callback(*printerInfo);
                    }

                    ELEGOO_LOG_INFO("Discovered {} printer: {} ({}) at {}",
                                    printerInfo->brand, printerInfo->name,
                                    StringUtils::maskString(printerInfo->printerId), printerInfo->host);
                }
                return; // Exit when matching strategy is found
            }
        }
    }

    bool PrinterDiscovery::isPrinterAlreadyDiscovered(const PrinterInfo &printer) const
    {
        // This function is kept for compatibility but uses optimized set lookup
        std::lock_guard<std::mutex> lock(printersMutex_);
        return discoveredPrinterIds_.find(printer.printerId) != discoveredPrinterIds_.end();
    }

    bool PrinterDiscovery::sendDiscoveryBroadcast(int port, const std::string &message)
    {
        auto addresses = NetworkUtils::getBroadcastAddresses();
        bool sentAny = false;

        try
        {
            ELEGOO_LOG_DEBUG("Attempting to send discovery broadcast to {} addresses on port {}", addresses.size(), port);

            // Traverse all broadcast addresses
            for (const auto &broadcastAddress : addresses)
            {
                sockaddr_in broadcastAddr;
                memset(&broadcastAddr, 0, sizeof(broadcastAddr));
                broadcastAddr.sin_family = AF_INET;
                broadcastAddr.sin_port = htons(port);

#if defined(__APPLE__) || defined(__linux__)
                // Use inet_addr to convert broadcast address
                in_addr_t addr = inet_addr(broadcastAddress.broadcast.c_str());
#else
                // Windows uses inet_pton
                unsigned long addr = inet_addr(broadcastAddress.broadcast.c_str());
#endif
                if (addr == INADDR_NONE)
                {
                    ELEGOO_LOG_WARN("Invalid broadcast address: {}", broadcastAddress.broadcast);
                    continue;
                }
                broadcastAddr.sin_addr.s_addr = addr;

                // Send broadcast
                int result = sendto(udpSocket_, message.c_str(), static_cast<int>(message.length()), 0,
                                    (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));

                if (result != SOCKET_ERROR)
                {
                    ELEGOO_LOG_DEBUG("Discovery broadcast sent to {}:{}",
                                     broadcastAddress.broadcast, port);
                    sentAny = true;
                }
                else
                {
#ifdef _WIN32
                    int error = WSAGetLastError();
                    ELEGOO_LOG_ERROR("Failed to send broadcast to {}:{}, error: {}",
                                     broadcastAddress.broadcast, port, error);
#else
                    ELEGOO_LOG_ERROR("Failed to send broadcast to {}:{}, error: {} ({})",
                                     broadcastAddress.broadcast, port, errno, strerror(errno));
#endif
                }
            }
            return sentAny;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error sending discovery broadcast: {}", e.what());
            return false;
        }
    }

    bool PrinterDiscovery::createUdpSocket()
    {
        closeUdpSocket();

        udpSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (udpSocket_ == INVALID_SOCKET)
        {
#ifdef _WIN32
            int error = WSAGetLastError();
            ELEGOO_LOG_ERROR("Failed to create UDP socket, error: {}", error);
#else
            ELEGOO_LOG_ERROR("Failed to create UDP socket, error: {} ({})", errno, strerror(errno));
#endif
            return false;
        }

        // Enable broadcast
        if (!NetworkUtils::enableBroadcast(static_cast<int>(udpSocket_)))
        {
            ELEGOO_LOG_ERROR("Failed to enable broadcast on socket");
            closeUdpSocket();
            return false;
        }

#ifndef _WIN32
        // Mac/Linux specific socket option settings
        int optval = 1;

        // Allow address reuse
        if (setsockopt(udpSocket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
        {
            ELEGOO_LOG_WARN("Failed to set SO_REUSEADDR: {} ({})", errno, strerror(errno));
        }

#ifdef SO_REUSEPORT
        // Mac supports SO_REUSEPORT
        if (setsockopt(udpSocket_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0)
        {
            ELEGOO_LOG_DEBUG("Failed to set SO_REUSEPORT: {} ({})", errno, strerror(errno));
        }
#endif

        // Set receive timeout
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        if (setsockopt(udpSocket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            ELEGOO_LOG_WARN("Failed to set receive timeout: {} ({})", errno, strerror(errno));
        }

        // Set send timeout
        if (setsockopt(udpSocket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            ELEGOO_LOG_WARN("Failed to set send timeout: {} ({})", errno, strerror(errno));
        }
#else
        // Set timeout (Windows version)
        if (!NetworkUtils::setSocketTimeout(static_cast<int>(udpSocket_), 5000))
        {
            ELEGOO_LOG_WARN("Warning: Failed to set socket timeout");
        }
#endif

        ELEGOO_LOG_DEBUG("UDP socket created successfully with descriptor: {}", static_cast<int>(udpSocket_));
        return true;
    }

    void PrinterDiscovery::closeUdpSocket()
    {
        if (udpSocket_ != INVALID_SOCKET)
        {
#ifdef _WIN32
            closesocket(udpSocket_);
#else
            close(udpSocket_);
#endif
            udpSocket_ = INVALID_SOCKET;
            ELEGOO_LOG_DEBUG("UDP socket closed");
        }
    }

    void PrinterDiscovery::cleanupDiscoveryState()
    {
        // Safely get and clear completion callback
        DiscoveryCompletionCallback completionCallback;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            completionCallback = completionCallback_;
            completionCallback_ = nullptr;
        }
        
        // Get discovered printers before cleanup for completion callback
        std::vector<PrinterInfo> finalPrinters;
        if (completionCallback)
        {
            std::lock_guard<std::mutex> lock(printersMutex_);
            finalPrinters = discoveredPrinters_;
        }

        // Close UDP socket (prevent duplicate closing)
        closeUdpSocket();

        // Reset state variables
        shouldStop_ = false;
        isDiscovering_ = false;

        // Call completion callback if set
        if (completionCallback)
        {
            try
            {
                completionCallback(finalPrinters);
                ELEGOO_LOG_DEBUG("Discovery completion callback invoked with {} printers", finalPrinters.size());
            }
            catch (const std::exception &e)
            {
                ELEGOO_LOG_ERROR("Error in completion callback: {}", e.what());
            }
            catch (...)
            {
                ELEGOO_LOG_ERROR("Unknown error in completion callback");
            }
        }

        ELEGOO_LOG_DEBUG("Discovery state cleaned up");
    }

    bool PrinterDiscovery::bindToAvailablePort()
    {
        sockaddr_in listenAddr;
        memset(&listenAddr, 0, sizeof(listenAddr));
        listenAddr.sin_family = AF_INET;
        listenAddr.sin_addr.s_addr = INADDR_ANY;

        // 1. First try user-specified preferred listen ports
        if (!config_.preferredListenPorts.empty())
        {
            for (int port : config_.preferredListenPorts)
            {
                listenAddr.sin_port = htons(port);
                
                if (bind(udpSocket_, (struct sockaddr *)&listenAddr, sizeof(listenAddr)) == 0)
                {
                    ELEGOO_LOG_INFO("Successfully bound to preferred port: {}", port);
                    return true;
                }
                else
                {
#ifdef _WIN32
                    int error = WSAGetLastError();
                    if (error == WSAEADDRINUSE)
                    {
                        ELEGOO_LOG_DEBUG("Preferred port {} is already in use, trying next...", port);
                    }
                    else
                    {
                        ELEGOO_LOG_WARN("Failed to bind to preferred port {}: {}", port, error);
                    }
#else
                    if (errno == EADDRINUSE)
                    {
                        ELEGOO_LOG_DEBUG("Preferred port {} is already in use, trying next...", port);
                    }
                    else
                    {
                        ELEGOO_LOG_WARN("Failed to bind to preferred port {}: {} ({})", port, errno, strerror(errno));
                    }
#endif
                }
            }
            ELEGOO_LOG_INFO("All preferred ports are unavailable, falling back to system-assigned port");
        }

        // 2. Fall back to system-assigned port (port 0) for maximum reliability
        listenAddr.sin_port = htons(0); // Let system assign an available port

        if (bind(udpSocket_, (struct sockaddr *)&listenAddr, sizeof(listenAddr)) == 0)
        {
            // Get the actual port assigned by the system
            socklen_t addrLen = sizeof(listenAddr);
            if (getsockname(udpSocket_, (struct sockaddr *)&listenAddr, &addrLen) == 0)
            {
                int assignedPort = ntohs(listenAddr.sin_port);
                ELEGOO_LOG_INFO("Successfully bound to system-assigned port: {}", assignedPort);
            }
            else
            {
                ELEGOO_LOG_INFO("Successfully bound to system-assigned port (port number unknown)");
            }
            return true;
        }
        else
        {
            ELEGOO_LOG_ERROR("Failed to bind to system-assigned port: {}", getLastSocketError());
            return false;
        }
    }

    std::string PrinterDiscovery::getLastSocketError() const
    {
#ifdef _WIN32
        int error = WSAGetLastError();
        return "Error code: " + std::to_string(error);
#else
        return std::string(strerror(errno)) + " (" + std::to_string(errno) + ")";
#endif
    }

} // namespace elink
