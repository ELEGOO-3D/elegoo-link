#pragma once

#include "printer.h"
namespace elink 
{

    /**
     * Parameters for retrieving the current printer list
     */
    struct GetPrinterListParams
    {
    };
    /**
     * Printer list data
     */
    struct GetPrinterListData
    {
        std::vector<PrinterInfo> printers; // List of printers
    };

    using GetPrinterListResult = BizResult<GetPrinterListData>;


    /**
     * Printer discovery configuration
     */
    struct PrinterDiscoveryParams
    {
        int timeoutMs = 5000;                  // Discovery timeout in milliseconds
        int broadcastInterval = 2000;          // Resend interval changed to 2 seconds to ensure resending within 5 seconds
        bool enableAutoRetry = false;          // Whether to resend discovery messages periodically
        std::vector<int> preferredListenPorts; // Optional: User-specified list of preferred listening ports
    };
    /**
     * Printer discovery data
     */
    struct PrinterDiscoveryData
    {
        std::vector<PrinterInfo> printers; // List of discovered printers
    };

    using PrinterDiscoveryResult = BizResult<PrinterDiscoveryData>;

} // namespace elink
