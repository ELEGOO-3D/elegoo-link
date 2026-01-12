#pragma once

#include <memory>
#include "type.h"
#include "core/base_printer.h"

namespace elink
{
    /**
     * PrinterFactory - Factory class for creating printer instances
     * Creates appropriate printer subclass based on printer type
     */
    class PrinterFactory
    {
    public:
        /**
         * Create a printer instance based on printer type
         * @param printerInfo Printer information
         * @return Smart pointer to printer instance, or nullptr if type not supported
         */
        static std::shared_ptr<BasePrinter> createPrinter(const PrinterInfo &printerInfo);

    private:
        PrinterFactory() = delete; // Prevent instantiation
    };

} // namespace elink
