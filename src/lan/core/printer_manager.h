#pragma once

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include "type.h"
#include "printer.h"
#include "types/internal/internal.h"
namespace elink 
{
    /**
     * Printer Manager (Redesigned)
     * Responsibilities:
     * 1. Printer Factory: Create and destroy printer instances
     * 2. Global Monitoring: Monitor the status changes of all printers
     * 3. Resource Management: Manage global resources such as connection pools and thread pools
     * 4. Event Dispatch: Dispatch global events and callbacks
     */
    class PrinterManager
    {
    public:
        PrinterManager();
        ~PrinterManager();

        /**
         * Initialize the printer manager
         * @return true if successful
         */
        bool initialize();

        /**
         * Clean up resources
         */
        void cleanup();

        // ========== Printer Factory ==========

        /**
         * Create a printer instance
         * @param printerInfo Printer information
         * @return Smart pointer to the printer, returns nullptr on failure
         */
        PrinterPtr createPrinter(const PrinterInfo &printerInfo);

        /**
         * Get a printer instance
         * @param printerId Printer ID
         * @return Smart pointer to the printer, returns nullptr if not found
         */
        PrinterPtr getPrinter(const std::string &printerId);

        /**
         * Remove a printer instance
         * @param printerId Printer ID
         * @return true if successful
         */
        bool removePrinter(const std::string &printerId);

        /**
         * Add a connected printer to the printer list
         * @param printer Smart pointer to the connected printer
         * @return true if successful
         */
        bool addConnectedPrinter(PrinterPtr printer);

        /**
         * Get all printers
         * @return List of smart pointers to printers
         */
        std::vector<PrinterPtr> getAllPrinters();

        /**
         * Get connected printers
         * @return List of smart pointers to connected printers
         */
        std::vector<PrinterPtr> getConnectedPrinters();

        // ========== Batch Operations ==========

        /**
         * Disconnect all printers
         */
        void disconnectAllPrinters();

        // ========== Global Callback Settings ==========

        /**
         * Set callback for printer connection status changes
         * @param callback Callback function (printerId, connected)
         */
        void setPrinterConnectionCallback(std::function<void(const std::string &, bool)> callback);
        /**
         * Set callback for printer status (shared by all printers)
         * @param callback Callback function
         */
        void setPrinterEventCallback(std::function<void(const BizEvent &)> callback);

        std::vector<PrinterInfo> getCachedPrinters() const;
    private:
        // Printer mapping table
        std::map<std::string, PrinterPtr> printers_;
        mutable std::mutex printersMutex_;

        // Global callback functions
        std::function<void(const std::string &, bool)> connectionCallback_;
        std::function<void(const BizEvent &)> eventCallback;
        std::mutex callbackMutex_;

        // Manager state
        bool initialized_;
    };

} // namespace elink
