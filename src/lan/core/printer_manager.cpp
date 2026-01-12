#include "core/printer_manager.h"
#include "core/base_printer.h"
#include "core/printer_factory.h"
#include "protocols/protocol_interface.h"
#include "utils/utils.h"
#include "utils/logger.h"
#include <iostream>
#include <algorithm>

namespace elink
{

    PrinterManager::PrinterManager()
        : initialized_(false)
    {
    }

    PrinterManager::~PrinterManager()
    {
        cleanup();
    }

    bool PrinterManager::initialize()
    {
        if (initialized_)
        {
            return true;
        }

        try
        {
            initialized_ = true;
            ELEGOO_LOG_INFO("Printer manager initialized successfully");
            return true;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to initialize printer manager: {}", e.what());
            return false;
        }
    }

    void PrinterManager::cleanup()
    {
        if (!initialized_)
        {
            return;
        }

        try
        {
            // Disconnect all printer connections
            disconnectAllPrinters();

            // Clean up printers
            {
                std::lock_guard<std::mutex> lock(printersMutex_);
                printers_.clear();
            }

            initialized_ = false;

            ELEGOO_LOG_INFO("Printer manager cleanup completed");
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error during printer manager cleanup: {}", e.what());
        }
    }

    // ========== Printer Factory ==========

    PrinterPtr PrinterManager::createPrinter(const PrinterInfo &printerInfo)
    {
        std::lock_guard<std::mutex> lock(printersMutex_);

        // Check if printer already exists
        if (printers_.find(printerInfo.printerId) != printers_.end())
        {
            ELEGOO_LOG_INFO("Printer {} already exists", StringUtils::maskString(printerInfo.printerId));
            return printers_[printerInfo.printerId];
        }

        try
        {
            // Create printer instance using factory
            auto printer = PrinterFactory::createPrinter(printerInfo);
            if (!printer)
            {
                ELEGOO_LOG_ERROR("Failed to create printer {} from factory", StringUtils::maskString(printerInfo.printerId));
                return nullptr;
            }

            // Set printer-level callback function
            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                if (eventCallback)
                {
                    printer->setEventCallback(eventCallback);
                }
            }

            printers_[printerInfo.printerId] = printer;

            ELEGOO_LOG_INFO("Printer {} created successfully", StringUtils::maskString(printerInfo.printerId));
            return printer;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to create printer {}: {}", StringUtils::maskString(printerInfo.printerId), e.what());
            return nullptr;
        }
    }

    PrinterPtr PrinterManager::getPrinter(const std::string &printerId)
    {
        std::lock_guard<std::mutex> lock(printersMutex_);

        auto it = printers_.find(printerId);
        return it != printers_.end() ? it->second : nullptr;
    }

    bool PrinterManager::removePrinter(const std::string &printerId)
    {
        std::lock_guard<std::mutex> lock(printersMutex_);

        auto it = printers_.find(printerId);
        if (it == printers_.end())
        {
            ELEGOO_LOG_ERROR("Printer {} not found", StringUtils::maskString(printerId));
            return false;
        }

        // Disconnect printer connection
        auto printer = it->second;
        if (printer && printer->isConnected())
        {
            (void)printer->disconnect(); // Ignore return value
        }

        printers_.erase(it);
        ELEGOO_LOG_INFO("Printer {} removed from manager", StringUtils::maskString(printerId));
        return true;
    }

    bool PrinterManager::addConnectedPrinter(PrinterPtr printer)
    {
        if (!printer)
        {
            ELEGOO_LOG_ERROR("Cannot add null printer to manager");
            return false;
        }

        std::lock_guard<std::mutex> lock(printersMutex_);
        const std::string &printerId = printer->getId();

        // Check if printer already exists
        if (printers_.find(printerId) != printers_.end())
        {
            ELEGOO_LOG_INFO("Printer {} already exists in manager, replacing it", StringUtils::maskString(printerId));
        }

        // Set printer-level callback function
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (eventCallback)
            {
                printer->setEventCallback(eventCallback);
            }
        }

        printers_[printerId] = printer;
        ELEGOO_LOG_DEBUG("Printer {} added to manager", StringUtils::maskString(printerId));
        return true;
    }

    std::vector<PrinterPtr> PrinterManager::getAllPrinters()
    {
        std::lock_guard<std::mutex> lock(printersMutex_);

        std::vector<PrinterPtr> printers;
        for (const auto &[printer_id, printer] : printers_)
        {
            printers.push_back(printer);
        }

        return printers;
    }

    std::vector<PrinterPtr> PrinterManager::getConnectedPrinters()
    {
        std::lock_guard<std::mutex> lock(printersMutex_);

        std::vector<PrinterPtr> connected_printers;
        for (const auto &[printer_id, printer] : printers_)
        {
            if (printer && printer->isConnected())
            {
                connected_printers.push_back(printer);
            }
        }

        return connected_printers;
    }

    // ========== Batch Operations ==========

    void PrinterManager::disconnectAllPrinters()
    {
        auto printers = getAllPrinters();

        for (auto &printer : printers)
        {
            if (printer && printer->isConnected())
            {
                printer->setEventCallback(nullptr); // Clear printer status callback
                (void)printer->disconnect();        // Ignore return value
            }
        }

        ELEGOO_LOG_INFO("Disconnected all printers");
    }
    std::vector<PrinterInfo> PrinterManager::getCachedPrinters() const
    {
        std::lock_guard<std::mutex> lock(printersMutex_);

        std::vector<PrinterInfo> printerInfos;
        for (const auto &[printer_id, printer] : printers_)
        {
            if (printer)
            {
                printerInfos.push_back(printer->getPrinterInfo());
            }
        }

        return printerInfos;
    }
    // ========== Global Callback Settings ==========

    void PrinterManager::setPrinterConnectionCallback(std::function<void(const std::string &, bool)> callback)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        connectionCallback_ = callback;
    }

    void PrinterManager::setPrinterEventCallback(std::function<void(const BizEvent &)> callback)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        eventCallback = callback;
    }
} // namespace elink
