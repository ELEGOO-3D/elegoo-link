#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <map>
#include <unordered_set>
#include "type.h"
#include "types/internal/internal.h"
#ifdef _WIN32
#include <winsock2.h>
#endif

namespace elink 
{
    
    // Event callback type
    using PrinterDiscoveredCallback = std::function<void(const PrinterInfo &)>;
    
    // Discovery completion callback type
    using DiscoveryCompletionCallback = std::function<void(const std::vector<PrinterInfo> &)>;

    // Forward declaration
    class PrinterParser;

    /**
     * Printer Discovery Strategy Interface
     */
    class IDiscoveryStrategy
    {
    public:
        virtual ~IDiscoveryStrategy() = default;
        virtual std::string getDiscoveryMessage() const = 0;
        virtual int getDefaultPort() const = 0;
        virtual std::string getBrand() const = 0;
        virtual std::unique_ptr<PrinterInfo> parseResponse(const std::string &response,
                                                          const std::string &senderIp,
                                                          int senderPort) const = 0;
        /**
         * Get the connection URL and Web interface URL of the printer
         * @param host Hostname or IP address
         * @param port Port number
         */
        virtual std::string getWebUrl(const std::string &host, int port) const = 0;
        // Supported authorization mode
        virtual std::string getSupportedAuthMode() const = 0;
    };

    /**
     * Printer Discovery Configuration
     */
    struct DiscoveryConfig
    {
        int timeoutMs = 5000;                  // Discovery timeout in milliseconds
        int broadcastInterval = 2000;          // Resend interval changed to 2 seconds to ensure resending within 5 seconds
        bool enableAutoRetry = false;          // Whether to resend discovery messages periodically
        std::vector<int> preferredListenPorts; // Optional: User-specified list of preferred listening ports
    };

    /**
     * Printer Discovery Interface
     * Supports UDP broadcast to discover various brands of printers in the local network
     */
    class PrinterDiscovery
    {
    public:
        PrinterDiscovery();
        ~PrinterDiscovery();

        /**
         * Add a printer discovery strategy
         * @param strategy Discovery strategy
         */
        void addDiscoveryStrategy(std::unique_ptr<IDiscoveryStrategy> strategy);

        /**
         * Start printer discovery
         * @param callback Callback function when a printer is discovered
         * @param config Discovery configuration
         * @param completionCallback Callback function when discovery is completed
         * @return true if discovery started successfully
         */
        bool startDiscovery(const DiscoveryConfig &config,
                            PrinterDiscoveredCallback callback,
                            DiscoveryCompletionCallback completionCallback = nullptr);

        /**
         * Stop printer discovery
         */
        void stopDiscovery();

        /**
         * Check if discovery is in progress
         * @return true if discovering
         */
        bool isDiscovering() const;

        /**
         * Get the list of discovered printers
         * @return List of printers
         */
        std::vector<PrinterInfo> getDiscoveredPrinters() const;

        /**
         * Clear the list of discovered printers
         */
        void clearDiscoveredPrinters();

        /**
         * Blocking printer discovery (synchronous)
         * @param config Discovery configuration
         * @return List of discovered printers
         */
        std::vector<PrinterInfo> discoverPrintersSync(const DiscoveryConfig &config = DiscoveryConfig{});

        /**
         * Get discovery strategy by printer type (static method for creating strategies)
         * @param printerType Printer type
         * @return Discovery strategy instance
         */
        static std::unique_ptr<IDiscoveryStrategy> getDiscoveryStrategy(PrinterType printerType);

    private:
        void discoveryThread();
        void processUdpResponse(const std::string &data, const std::string &senderIp, int senderPort);
        bool createUdpSocket();
        void closeUdpSocket();
        void cleanupDiscoveryState();
        bool sendDiscoveryBroadcast(int port, const std::string &message);
        bool isPrinterAlreadyDiscovered(const PrinterInfo &printer) const;
        bool receiveResponses();
        void sendBroadcastToAllPorts();
        bool bindToAvailablePort();
        std::string getLastSocketError() const; // Get last socket error message

    private:
        // Atomic state variables (declare first, destruct last)
        std::atomic<bool> isDiscovering_;
        std::atomic<bool> shouldStop_;

        // Synchronization objects
        mutable std::mutex printersMutex_;
        mutable std::mutex callbackMutex_; // Protect callback functions

        // Configuration and callback
        PrinterDiscoveredCallback discoveryCallback_;
        DiscoveryCompletionCallback completionCallback_;
        DiscoveryConfig config_;
        std::vector<int> ports_; // Current discovery port list

        // Network-related resources
#ifdef _WIN32
        SOCKET udpSocket_;
#else
        int udpSocket_;
#endif

        // Data storage
        std::vector<PrinterInfo> discoveredPrinters_;
        std::unordered_set<std::string> discoveredPrinterIds_; // For fast duplicate checking
        std::vector<std::unique_ptr<IDiscoveryStrategy>> discoveryStrategies_;

        // Thread object (declare last, destruct first)
        std::thread discoveryThread_;
    };

} // namespace elink
