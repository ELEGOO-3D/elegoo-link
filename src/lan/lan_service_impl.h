#pragma once

#include "lan_service.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <chrono>
#include <unordered_set>
#include "type.h"
#include "events/event_system.h"
#include "types/internal/internal.h"
#include "static_web_server.h"
namespace elink 
{
    // Forward declarations
    class PrinterManager;
    class PrinterDiscovery;
    class BasePrinterAdapter;
    class BasePrinter;
    struct LogConfig;

    /**
     * Private implementation class of LanService
     * Hides all private members and implementation details
     */
    class LanServiceImpl
    {
    public:
        /**
         * Constructor
         */
        LanServiceImpl();

        /**
         * Destructor
         */
        ~LanServiceImpl();

        // ========== Internal Implementation Methods ==========

        /**
         * Initialize the logging system
         * @param config Logging configuration
         * @return true if successful
         */
        bool initializeLogger(const LogConfig &config);

        /**
         * Initialize adapters
         * @return true if successful
         */
        bool initializeAdapters();

        /**
         * Filter unregistered printers
         * @param printers List of printers
         * @return List of unregistered printers
         */
        std::vector<PrinterInfo> filterUnregisteredPrinters(const std::vector<PrinterInfo> &printers) const;

        /**
         * Check existing connections
         * @param printerId Printer ID
         * @return Response result if the printer is already connected, otherwise nullopt
         */
        std::optional<ConnectPrinterResult> checkExistingConnection(const std::string &printerId) const;

        /**
         * Find a printer from the discovery list
         * @param printerId Printer ID
         * @return Printer information, or nullopt if not found
         */
        std::optional<PrinterInfo> findDiscoveredPrinter(const std::string &printerId) const;

        /**
         * Create printer information from connection parameters
         * @param params Connection parameters
         * @return Printer information
         */
        PrinterInfo createPrinterInfoFromParams(const ConnectPrinterParams &params) const;

        /**
         * Create and connect a printer
         * @param printerInfo Printer information
         * @param params Connection parameters
         * @param addOnlyIfConnected Whether to add to the printer list only if the connection is successful
         * @return Connection result
         */
        ConnectPrinterResult createAndConnectPrinter(const PrinterInfo &printerInfo,
                                                     const ConnectPrinterParams &params,
                                                     bool addOnlyIfConnected);

        /**
         * General printer validation and retrieval for file operations
         * @param printerId Printer ID
         * @return Printer pointer, or nullptr if validation fails, along with the corresponding error result
         */
        std::pair<std::shared_ptr<BasePrinter>, VoidResult> validateAndGetPrinter(const std::string& printerId);

        /**
         * General printer validation and retrieval (without checking connection status)
         * @param printerId Printer ID
         * @return Printer pointer, or nullptr if validation fails, along with the corresponding error result
         */
        std::pair<std::shared_ptr<BasePrinter>, VoidResult> validateAndGetPrinterBasic(const std::string& printerId);
    public:
        // ========== Member Variables ==========
        LanService::Config config_;                                      // Configuration information
        bool initialized_;                                               // Whether it has been initialized
        std::shared_ptr<PrinterManager> printerManager_;                   // Printer manager
        std::shared_ptr<PrinterDiscovery> printerDiscovery_;               // Printer discovery
        std::unique_ptr<StaticWebServer> server_;                       // Static web server

        // Track printers currently being connected
        std::unordered_set<std::string> connectingPrinters_;            // Set of printer IDs currently being connected
        std::mutex connectingPrintersMutex_;                            // Mutex for thread-safe access to connectingPrinters_
    };

} // namespace elink
