#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include "type.h"
#include "events/event_system.h"
#include "elegoo_export.h"

namespace elink
{
    // Forward declarations
    class LanServiceImpl;
    class BasePrinter;
    using PrinterPtr = std::shared_ptr<BasePrinter>;

    /**
     * LAN Service - Local Area Network Printer Control
     *
     * This class provides local network (LAN) printer discovery, connection, and management.
     * It enables direct communication with Elegoo printers on the same local network without cloud services.
     *
     * Main features:
     * 1. mDNS-based printer discovery on local network
     * 2. Direct printer connection via WebSocket/MQTT
     * 3. Local printer management (add, remove printers)
     * 4. Real-time printer status monitoring
     * 5. File transfer and print control
     */
    class ELEGOO_LINK_API LanService
    {
    public:
        /**
         * LanService initialization configuration
         */
        struct Config
        {
            Config() :enableWebServer(false), webServerPort(32538) {}
            // Web server configuration
            // Whether to enable static web server, if enabled, the static web server will serve the Vue compiled web pages
            // This is useful for displaying the SDK's web interface in the software
            bool enableWebServer;      // Whether to enable static web server
            int webServerPort;         // Static web server port
            std::string staticWebPath; // Path to static web files, if empty, no static web server will be started
        };

        /**
         * File upload progress callback function type
         * @param printerId Printer ID
         * @param taskId Upload task ID
         * @param progress Progress data
         * @return Return false to cancel upload, return true to continue upload
         */
        using FileUploadProgressCallback = std::function<bool(const FileUploadProgressData &progress)>;

        /**
         * Event subscription ID type
         */
        using EventSubscriptionId = EventBus::EventId;

    public:
        /**
         * Get singleton instance
         * @return Reference to LanService singleton instance
         */
        static LanService &getInstance();

        /**
         * Destructor
         */
        ~LanService();

        // Disable copy constructor and assignment
        LanService(const LanService &) = delete;
        LanService &operator=(const LanService &) = delete;
        /**
         * Initialize LanService
         * @return true if successful
         */
        bool initialize(const Config &config = Config());

        /**
         * Cleanup resources
         */
        void cleanup();

        /**
         * Check if initialized
         * @return true if initialized
         */
        bool isInitialized() const;

        // ========== Printer discovery functions ==========

        /**
         * Start printer discovery
         * @param params Discovery parameters
         * @return Discovery result
         */
        BizResult<PrinterDiscoveryData> startPrinterDiscovery(const PrinterDiscoveryParams &params);

        /**
         * Start printer discovery asynchronously
         * @param params Discovery parameters
         * @param discoveredCallback Callback for discovered printers
         * @param completionCallback Callback for discovery completion
         * @return Operation result
         */
        VoidResult startPrinterDiscoveryAsync(const PrinterDiscoveryParams &params, std::function<void(const PrinterInfo &)> discoveredCallback,
                                             std::function<void(const std::vector<PrinterInfo> &)> completionCallback);

        /**
         * Stop printer discovery
         * @return Operation result
         */
        VoidResult stopPrinterDiscovery();

        /**
         * Get list of discovered printers (unregistered printers)
         * @return Printer list
         */
        std::vector<PrinterInfo> getDiscoveredPrinters() const;

        // ========== Printer connection functions ==========

        /**
         * Add printer to management list
         * The printer will be added to the list once created, regardless of connection success
         *
         * @param params Connection parameters
         * @return Operation result
         * @note Possible error codes:
         *
         * - `ELINK_ERROR_CODE::SUCCESS`: Connection successful
         *
         * - `ELINK_ERROR_CODE::INVALID_PARAMETER`: Invalid parameters (e.g., missing required fields)
         *
         * - `ELINK_ERROR_CODE::PRINTER_ALREADY_CONNECTED`: Printer is already connected or connecting
         *
         * - `ELINK_ERROR_CODE::INVALID_ACCESS_CODE`: Printer is not authorized
         *
         * - `ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR`: Other connection errors
         *
         * - Other errors as defined in ELINK_ERROR_CODE enum
         */
        ConnectPrinterResult connectPrinter(const ConnectPrinterParams &params);

        /**
         * Disconnect printer
         * After disconnection, the printer will be removed from the printer list
         *
         * @param params Printer base parameters
         * @return Operation result
         */
        VoidResult disconnectPrinter(const std::string &printerId);

        // ========== Printer management functions ==========
        /**
         * Get list of registered printers
         * @return Printer list response
         */
        GetPrinterListResult getPrinters();

        /**
         * Check if the printer is connected
         * @param printerId Printer ID
         */
        bool isPrinterConnected(const std::string &printerId) const;

        /**
         * Get printer information by printer ID
         * @param printerId Printer ID
         * @return Printer information, returns nullptr if not found
         */
        PrinterPtr getPrinter(const std::string &printerId);

        // ========== File upload functions ==========

        /**
         * Upload file to printer
         * @param params File upload parameters
         * @param progressCallback Progress callback function, return false to cancel upload
         * @return Operation result
         * * @note Possible error codes:
         *
         * - `ELINK_ERROR_CODE::SUCCESS`: Upload successful
         *
         * - `ELINK_ERROR_CODE::INVALID_PARAMETER`: Invalid parameters (e.g., missing required fields)
         *
         * - `ELINK_ERROR_CODE::FILE_NOT_FOUND`: File not found
         *
         * - `ELINK_ERROR_CODE::PRINTER_ACCESS_DENIED`: Printer access denied
         *
         * - `ELINK_ERROR_CODE::FILE_TRANSFER_FAILED`: File transfer failed
         *
         * - `ELINK_ERROR_CODE::PRINTER_BUSY`: Printer is busy
         *
         * - Other errors as defined in ELINK_ERROR_CODE enum
         */
        FileUploadResult uploadFile(
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback = nullptr);

        /**
         * Cancel file upload
         * @param params Cancel parameters (contains printerId)
         * @return Operation result
         */
        VoidResult cancelFileUpload(const CancelFileUploadParams &params);

        /**
         * Get printer attributes
         * @param params Printer attributes parameters
         * @param timeout Timeout in milliseconds
         */
        PrinterAttributesResult getPrinterAttributes(const PrinterAttributesParams &params, int timeout = 3000);
        /**
         * Get printer status
         * @param params Printer status parameters
         * @param timeout Timeout in milliseconds
         */
        PrinterStatusResult getPrinterStatus(const PrinterStatusParams &params, int timeout = 3000);

        /**
         * Refresh printer attributes, The result will be notified through events
         * @param params Printer attributes parameters
         * @return Operation result
         */
        VoidResult refreshPrinterAttributes(const PrinterAttributesParams &params);

        /**
         * Refresh printer status, The result will be notified through events
         * @param params Printer status parameters
         * @return Operation result
         */
        VoidResult refreshPrinterStatus(const PrinterStatusParams &params);

        // ========== Print control functions ==========

        /**
         * Start print task
         * @param params Print parameters
         * @return Operation result
         */
        VoidResult startPrint(const StartPrintParams &params);

        /**
         * Pause print
         * @param params Printer base parameters
         * @return Operation result
         */
        VoidResult pausePrint(const PausePrintParams &params);

        /**
         * Resume print
         * @param params Printer base parameters
         * @return Operation result
         */
        VoidResult resumePrint(const ResumePrintParams &params);

        /**
         * Stop print
         * @param params Printer base parameters
         * @return Operation result
         */
        VoidResult stopPrint(const StopPrintParams &params);

        /**
         * Get canvas status
         * This function retrieves the status of the canvas, including active canvas, trays, and auto refill status.
         * @param params Get canvas status parameters
         * @return Canvas status result
         */
        GetCanvasStatusResult getCanvasStatus(const GetCanvasStatusParams &params);

        VoidResult setAutoRefill(const SetAutoRefillParams &params);

        // ========== Callback management ==========
        /**
         * General strongly-typed event subscription method
         * @param handler Event handler function
         * @return Subscription ID, can be used to unsubscribe
         */
        template <typename EventType>
        EventSubscriptionId subscribeEvent(
            std::function<void(const std::shared_ptr<EventType> &)> handler)
        {
            return eventBus_.subscribe<EventType>(handler);
        }

        /**
         * Unsubscribe event
         * @param id Subscription ID
         * @return Whether unsubscribed successfully
         */
        template <typename EventType>
        bool unsubscribeEvent(EventSubscriptionId id)
        {
            return eventBus_.unsubscribe<EventType>(id);
        }

        void clearAllEventSubscriptions() { eventBus_.clear(); }
        
        /**
         * Set event callback (for compatibility with old callback style)
         * @param callback Event callback function
         */
        void setEventCallback(std::function<int(const BizEvent &)> callback);

        // ========== Utility methods ==========

        /**
         * Get version information
         * @return Version string
         */
        std::string getVersion() const;

        /**
         * Get list of supported printer types
         * @return Supported printer types
         */
        std::vector<PrinterType> getSupportedPrinterTypes() const;

        std::vector<PrinterInfo> getCachedPrinters() const;

        VoidResult updatePrinterName(const UpdatePrinterNameParams &params);
    private:
        /**
         * Private constructor (singleton pattern)
         */
        LanService();

    private:
        // ========== Member variables ==========

        EventBus eventBus_;                     // Strongly-typed event bus (needs to be accessed in template)
        std::unique_ptr<LanServiceImpl> pImpl_; // Pimpl pointer (used to hide private methods and other members)
    };

} // namespace elink
